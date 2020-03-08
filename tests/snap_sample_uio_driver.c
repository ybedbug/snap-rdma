/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#include "snap_sample_device.h"
#include "host_uio.h"

#define SAMPLE_DRIVER_DEFAULT_NUM_ITERS  256

static inline void sq_db(uint16_t pos)
{
	host_uio_memory_bus_store_fence();
	host_uio_write4(SNAP_SAMPLE_DEV_DB_BASE + SNAP_SAMPLE_DEV_SQ_DB_OFFSET,
			(uint32_t)pos);
	host_uio_memory_cpu_fence();
}

static inline void cq_db(uint16_t pos)
{
	host_uio_memory_bus_store_fence();
	host_uio_write4(SNAP_SAMPLE_DEV_DB_BASE + SNAP_SAMPLE_DEV_CQ_DB_OFFSET,
			pos);
	host_uio_memory_cpu_fence();
}

struct drv_queue {
	union {
		struct snap_sample_device_cqe *cq_base;
		struct snap_sample_device_sqe *sq_base;
	};
	uint16_t tail;
	uint16_t head;
	uint8_t  phase;
	uint32_t sn;

	uintptr_t base_pa;
};

struct sample_driver {
	struct drv_queue sq;
	struct drv_queue cq;
};

static void drv_queue_reset(struct drv_queue *q)
{
	q->tail = q->head = 0;
	q->phase = 1;
	q->sn = 0;
}

static inline uint16_t drv_queue_add(uint16_t pos, uint16_t n)
{
	return (pos + n) & (SNAP_SAMPLE_DEV_QUEUE_DEPTH - 1);
}

static inline void drv_queue_add_tail(struct drv_queue *q, uint16_t n)
{
	q->tail = drv_queue_add(q->tail, n);
}

static inline void drv_queue_add_head(struct drv_queue *q, uint16_t n)
{
	uint16_t new_head;

	new_head = drv_queue_add(q->head, n);
	if (new_head < q->head)
		q->phase = !q->phase;

	q->head = new_head;
}

static inline void drv_queue_set_head(struct drv_queue *q, uint16_t n)
{
	q->head = n;
}

static inline bool drv_queue_is_full(struct drv_queue *q)
{
	return (drv_queue_add(q->tail, 1) == q->head) ? true : false;
}

static inline struct snap_sample_device_cqe *drv_queue_wait(struct drv_queue *q)
{
	struct snap_sample_device_cqe *cqe;
	int poll_to = 1000000;

	printf("check q->head = %u\n", q->head);
	do {
		cqe = &q->cq_base[q->head];
		if (cqe->sn == q->sn + 1) {
			host_uio_memory_bus_load_fence();
			q->sn++;
			return cqe;
		}
		host_uio_compiler_fence();
	} while (--poll_to > 0);

	return NULL;
}

static inline bool drv_queue_is_empty(struct drv_queue *q)
{
	return q->tail == q->head ? true : false;
}

static int driver_cmd(enum snap_sample_device_cmds cmd)
{
	uint32_t status, dev_cmd;
	int to_ms;

	status = host_uio_read4(SNAP_SAMPLE_DEV_REG_CMD);
	status = ((status >> SNAP_SAMPLE_DEV_CMD_SHIFT) + 1) << SNAP_SAMPLE_DEV_CMD_SHIFT;
	dev_cmd = status | (cmd & SNAP_SAMPLE_DEV_CMD_MASK);
	host_uio_write4(SNAP_SAMPLE_DEV_REG_CMD, dev_cmd);

	to_ms = 0;
	do {
		usleep(1000);
		status = host_uio_read4(SNAP_SAMPLE_DEV_REG_CST);
		if (status == dev_cmd)
			return 0;
		to_ms++;
	} while(to_ms < SNAP_SAMPLE_DEV_CMD_TIMEOUT);

	printf("Command timeout for %d\n", cmd);
	return -1;
}

struct sample_driver *driver_init(char *pci_addr)
{
	int rc;
	struct sample_driver *drv;

	drv = calloc(sizeof(*drv), 1);
	if (!drv) {
		printf("Failed to allocate driver\n");
		return NULL;
	}

	rc = host_uio_open(pci_addr, 0);
	if (rc) {
		printf("Failed to open %s\n", pci_addr);
		goto free_driver;
	}

	rc = host_uio_dma_init();
	if (rc) {
		printf("Failed to initialize DMA memory\n");
		goto uio_close;
	}

	rc = driver_cmd(SNAP_SAMPLE_DEV_CMD_STOP);
	if (rc) {
		printf("Failed to stop device\n");
		goto dma_destroy;
	}

	drv->cq.cq_base = host_uio_dma_alloc(SNAP_SAMPLE_DEV_QUEUE_DEPTH *
			sizeof(struct snap_sample_device_sqe), &drv->cq.base_pa);
	if (!drv->cq.cq_base) {
		printf("Failed to allocate CQ DMA memory");
		goto dma_destroy;
	}
	memset(drv->cq.cq_base, 0, SNAP_SAMPLE_DEV_QUEUE_DEPTH *
			sizeof(struct snap_sample_device_cqe));
	host_uio_write8(SNAP_SAMPLE_DEV_REG_CQPA, drv->cq.base_pa);

	drv->sq.sq_base = host_uio_dma_alloc(SNAP_SAMPLE_DEV_QUEUE_DEPTH *
			sizeof(struct snap_sample_device_sqe), &drv->sq.base_pa);
	if (!drv->sq.sq_base) {
		printf("Failed to allocate SQ DMA memory");
		goto free_cq_mem;
	}
	memset(drv->sq.sq_base, 0, SNAP_SAMPLE_DEV_QUEUE_DEPTH *
			sizeof(struct snap_sample_device_sqe));
	host_uio_write8(SNAP_SAMPLE_DEV_REG_SQPA, drv->sq.base_pa);

	rc = driver_cmd(SNAP_SAMPLE_DEV_CMD_START);
	if (rc) {
		printf("Failed to start device\n");
		goto free_sq_mem;
	}

	drv_queue_reset(&drv->cq);
	drv_queue_reset(&drv->sq);
	return drv;

free_sq_mem:
	host_uio_dma_free(drv->sq.sq_base);
free_cq_mem:
	host_uio_dma_free(drv->cq.cq_base);
dma_destroy:
	host_uio_dma_destroy();
uio_close:
	host_uio_close();
free_driver:
	free(drv);
	return NULL;
}

static void driver_destroy(struct sample_driver *drv)
{
	driver_cmd(SNAP_SAMPLE_DEV_CMD_STOP);
	host_uio_dma_free(drv->sq.sq_base);
	host_uio_dma_free(drv->cq.cq_base);
	host_uio_dma_destroy();
	host_uio_close();
	free(drv);
}

static void driver_run_ping_pong(struct sample_driver *drv, unsigned n_iters, int do_dma)
{
	struct snap_sample_device_cqe *cqe;
	struct snap_sample_device_sqe *sqe;
	int i, rc;
	void *va;
	uint64_t pa;

	if (do_dma) {
		va = host_uio_dma_alloc(SNAP_SAMPLE_DEV_MAX_DATA_SIZE, &pa);
		if (!va) {
			printf("failed to allocated dma memory for data transfer");
			return;
		}
	}

	for (i = 0; i < n_iters; i++) {
		/* send ping */
		sqe = &drv->sq.sq_base[drv->sq.tail];
		sqe->sn = i;
		if (do_dma) {
			memset(va, 0, SNAP_SAMPLE_DEV_MAX_DATA_SIZE);
			sqe->len = SNAP_SAMPLE_DEV_MAX_DATA_SIZE;
			sqe->paddr = pa;
		};
		drv_queue_add_tail(&drv->sq, 1);
		sq_db(drv->sq.tail);

		/* wait for pong */
		cqe = drv_queue_wait(&drv->cq);
		if (!cqe) {
			printf("timeout waiting for the ping reply\n");
			return;
		}
		printf("pong, sn = %u\n", cqe->sn);
		if (do_dma) {
			if (memchr(va, 0, SNAP_SAMPLE_DEV_MAX_DATA_SIZE))
				printf("data check failed\n");
		}

		/* ack completion */
		drv_queue_add_head(&drv->sq, 1);
		drv_queue_add_head(&drv->cq, 1);
		cq_db(drv->cq.head);
	}

	if (do_dma)
		host_uio_dma_free(va);
}

static void driver_run_pong_ping(struct sample_driver *drv, unsigned n_iters)
{
	struct snap_sample_device_cqe *cqe;
	struct snap_sample_device_sqe *sqe;
	int i, rc;

	rc = driver_cmd(SNAP_SAMPLE_DEV_CMD_RUN_PING_TEST);

	if (rc) {
		printf("Failed to stop device\n");
		return;
	}

	for (i = 0; i < n_iters; i++) {
		/* wait for pong */
		cqe = drv_queue_wait(&drv->cq);
		if (!cqe) {
			printf("timeout waiting for the ping reply\n");
			return;
		}
		printf("ping, sn = %u\n", cqe->sn);

		/* ack completion */
		drv_queue_add_head(&drv->sq, 1);
		drv_queue_add_head(&drv->cq, 1);
		printf("ring cq db %u\n", drv->cq.head);
		cq_db(drv->cq.head);

		if (i >= n_iters - 1)
			break;

		/* send pong */
		sqe = &drv->sq.sq_base[drv->sq.tail];
		sqe->sn = cqe->sn;
		drv_queue_add_tail(&drv->sq, 1);
		printf("ring sq db %u\n", drv->sq.tail);
		sq_db(drv->sq.tail);
	}
}

static void usage()
{
	printf("Usage: snap_samp_uio_driver -r -t <test> <bdf>\n"
		"\t-t tests are:\n"
		"\t\tping - ping test, where ping is initiated by the driver\n"
		"\t\tpong - ping test, where ping is initiated by the device\n"
		"\t-r do dma operation for each descriptor (ping test only)\n"
		"\t-n <n> number of iterations for ping and pong tests. Default is %d\n",
		SAMPLE_DRIVER_DEFAULT_NUM_ITERS
		);
}

int main(int argc, char **argv)
{
	int rc;
	struct sample_driver *drv;
	char test_name[16] = {};
	int opt, do_dma = 0;
	unsigned n_iters = SAMPLE_DRIVER_DEFAULT_NUM_ITERS;

	while ((opt = getopt(argc, argv, "hrt:n:")) != -1) {
		switch (opt) {
			case 't':
				strncpy(test_name, optarg, sizeof(test_name));
				break;
			case 'r':
				do_dma = 1;
				break;
			case 'n':
				n_iters = atoi(optarg);
				break;
			case 'h':
			default:
				usage();
				exit(1);
		}
	}

	if (optind >= argc) {
		usage();
		exit(1);
	}

	drv = driver_init(argv[optind]);
	if (!drv)
		exit(1);

	if (strcmp(test_name, "ping") == 0) {
		printf("running ping pong\n");
		driver_run_ping_pong(drv, n_iters, do_dma);
	} else if (strcmp(test_name, "pong") == 0) {
		printf("running pong ping\n");
		driver_run_pong_ping(drv, n_iters);
	} else {
		printf("Unknown test name %s\n", test_name);
	}

	driver_destroy(drv);
	return 0;
}
