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

#ifndef VIRTIO_BLK_EXT_H
#define VIRTIO_BLK_EXT_H
#include <linux/virtio_blk.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#ifndef VIRTIO_CONFIG_S_NEEDS_RESET
#define VIRTIO_CONFIG_S_NEEDS_RESET 0x40
#endif

#endif
