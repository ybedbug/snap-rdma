/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_JSON_RPC_CLIENT_H
#define SNAP_JSON_RPC_CLIENT_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>

#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define SNAP_JSON_RPC_RECV_BUF_SIZE_INIT (8 * 1024)
#define SNAP_JSON_RPC_RECV_BUF_MAX_SIZE (128 * 1024)

struct snap_json_rpc_client_response {
	char *buf;
	size_t length;
};

struct snap_json_rpc_client {
	int sockfd;
	bool connected;

	bool rsp_ready;
	size_t recv_buf_size;
	size_t recv_offset;
	char *recv_buf;

	size_t send_len;
	size_t send_offset;
	char *send_buf;

};

struct snap_json_rpc_client *snap_json_rpc_client_open(const char *addr);
void snap_json_rpc_client_close(struct snap_json_rpc_client *client);
int snap_json_rpc_client_send_req(struct snap_json_rpc_client *client,
				  void *buf, size_t length);
int snap_json_rpc_wait_for_response(struct snap_json_rpc_client *client);
void snap_json_rpc_client_reset_send_buf(struct snap_json_rpc_client *client);
void snap_json_rpc_put_response(struct snap_json_rpc_client_response *rsp);
struct snap_json_rpc_client_response*
snap_json_rpc_get_response(struct snap_json_rpc_client *client);

#endif
