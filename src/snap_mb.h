/*
 * Copyright © 202 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_MB_H
#define SNAP_MB_H

/* memory barriers */

#define snap_compiler_fence() asm volatile(""::: "memory")

#if defined(__x86_64__)

#define snap_memory_bus_fence()        asm volatile("mfence"::: "memory")
#define snap_memory_bus_store_fence()  asm volatile("sfence" ::: "memory")
#define snap_memory_bus_load_fence()   asm volatile("lfence" ::: "memory")

#define snap_memory_cpu_fence()        snap_compiler_fence()
#define snap_memory_cpu_store_fence()  snap_compiler_fence()
#define snap_memory_cpu_load_fence()   snap_compiler_fence()

#elif defined(__aarch64__)

#define snap_memory_bus_fence()        asm volatile("dsb sy" ::: "memory")
//#define snap_memory_bus_store_fence()  asm volatile("dsb st" ::: "memory")
//#define snap_memory_bus_load_fence()   asm volatile("dsb ld" ::: "memory")
//
/* The macro is used to serialize stores across Normal NC (or Device) and WB
 * memory, (see Arm Spec, B2.7.2).  Based on recent changes in Linux kernel:
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=22ec71615d824f4f11d38d0e55a88d8956b7e45f
 *
 * The underlying barrier code was changed to use lighter weight DMB instead
 * of DSB. The barrier used for synchronization of access between write back
 * and device mapped memory (PCIe BAR).
 *
 * According to vkleiner@nvidia.com
 * - improvements of around couple-hundreds kIOPS (more or less, depending
 *   on the workload) for 8 active BlueField cores with the following change
 * - improvement to parrallel fraction on 512B test
 */
/* does not seem to compile with all compilers. revert to 'dsb' version */
//#define snap_memory_bus_fence()        asm volatile("dmb oshsy" ::: "memory")
#define snap_memory_bus_store_fence()  asm volatile("dmb oshst" ::: "memory")
#define snap_memory_bus_load_fence()   asm volatile("dmb oshld" ::: "memory")

#define snap_memory_cpu_fence()        asm volatile("dmb ish" ::: "memory")
#define snap_memory_cpu_store_fence()  asm volatile("dmb ishst" ::: "memory")
#define snap_memory_cpu_load_fence()   asm volatile("dmb ishld" ::: "memory")

#elif defined(__DPA) || defined(__riscv)

#define snap_riscv_fence(p, s) \
	asm volatile("fence " #p "," #s : : : "memory")

#define snap_memory_bus_fence() snap_riscv_fence(iorw, iorw)
#define snap_memory_bus_store_fence() snap_riscv_fence(ow, ow)
#define snap_memory_bus_load_fence() snap_riscv_fence(ir, ir)

#define snap_memory_cpu_fence() snap_riscv_fence(rw, rw)
#define snap_memory_cpu_store_fence() snap_riscv_fence(w, w)
#define snap_memory_cpu_load_fence() snap_riscv_fence(r, r)

#else
# error "Unsupported architecture"
#endif

#endif
