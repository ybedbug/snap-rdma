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

#ifndef HOST_UIO_H
#define HOST_UIO_H

#include <stdint.h>

int host_uio_open(const char *pci_addr, int bar);
void host_uio_close();

void host_uio_write4(unsigned bar_offset, uint32_t val);
void host_uio_write8(unsigned bar_offset, uint64_t val);
uint32_t host_uio_read4(unsigned bar_offset);
uint64_t host_uio_read8(unsigned bar_offset);

int host_uio_bus_master_enable();
int host_uio_bus_master_disable();

int host_uio_dma_init();
void host_uio_dma_destroy();
void *host_uio_dma_alloc(size_t size, uintptr_t *pa);
void host_uio_dma_free(void *buf);

/* memory barriers */

#define host_uio_compiler_fence() asm volatile(""::: "memory")

#if defined(__x86_64__)

#define host_uio_memory_bus_fence()        asm volatile("mfence"::: "memory")
#define host_uio_memory_bus_store_fence()  asm volatile("sfence" ::: "memory")
#define host_uio_memory_bus_load_fence()   asm volatile("lfence" ::: "memory")

#define host_uio_memory_cpu_fence()        host_uio_compiler_fence()
#define host_uio_memory_cpu_store_fence()  host_uio_compiler_fence()
#define host_uio_memory_cpu_load_fence()   host_uio_compiler_fence()

#elif defined(__aarch64__)

#define host_uio_memory_bus_fence()        asm volatile("dsb sy" ::: "memory")
#define host_uio_memory_bus_store_fence()  asm volatile("dsb st" ::: "memory")
#define host_uio_memory_bus_load_fence()   asm volatile("dsb ld" ::: "memory")

#define host_uio_memory_cpu_fence()        asm volatile("dmb ish" ::: "memory")
#define host_uio_memory_cpu_store_fence()  asm volatile("dmb ishst" ::: "memory")
#define host_uio_memory_cpu_load_fence()   asm volatile("dmb ishld" ::: "memory")

#else
# error "Unsupported architecture"
#endif

#endif
