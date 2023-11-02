/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Parts of this file are derived from material that is
 * * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * @brief VIRTIO API
 * @defgroup virtio VIRTIO API
 * @ingroup io_interfaces
 *
 * This module contains functions and structures used to implement
 * VIRTIO drivers.
 *
 * @{
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_
#define ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_

#include <zephyr/device.h>
#include <zephyr/net/net_ip.h>

#include <openamp/virtqueue.h>
#include <openamp/virtio.h>
#include <openamp/virtio_mmio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct virtio_device* (*virt_get_virtio_device)(const struct device *dev);

__subsystem struct virtio_driver_api {
    virt_get_virtio_device get_virtio_device;
};

static inline struct virtio_device* virtio_get_vmmio_dev(const struct device *dev)
{
    struct virtio_device *vdev = ((struct virtio_driver_api*)dev->api)->get_virtio_device(dev);
    return vdev;
}

#define VIRTIO_RING_SIZE(n, align) \
	(( \
		( \
		sizeof(struct vring_desc) * n + \
		sizeof(struct vring_avail) + \
		sizeof(uint16_t) * (n + 1) + \
		align - 1 \
		) \
		& ~(align - 1) \
	) + \
	sizeof(struct vring_used) + \
	sizeof(struct vring_used_elem) * n + sizeof(uint16_t))

/**
 * @brief Declare a virtqueue structure.
 *
 * @param name	The name of the virtqueue structure.
 * @param n		Size of the virtqueue. Must be a power of 2.
 * @param align	Memory alignment of the associated vring structures.
 */
#define VIRTIO_MMIO_VQ_DECLARE(name, n, align) \
	static char __vrbuf_##name[VIRTIO_RING_SIZE(n, align)] __aligned(VIRTIO_MMIO_VRING_ALIGNMENT); \
	static struct { \
	struct virtqueue vq; \
	struct vq_desc_extra extra[n]; \
	} __vq_wrapper_##name = { \
	.vq = { \
		.vq_nentries = n, \
		.vq_ring = { \
		.desc = (void *)__vrbuf_##name, \
				.avail = (void *)((unsigned long)__vrbuf_##name + \
					n * sizeof(struct vring_desc)), \
				.used = (void *)((unsigned long)__vrbuf_##name + \
					((n * sizeof(struct vring_desc) + \
					(n + 1) * sizeof(uint16_t) + align - 1) & ~(align - 1))), \
		}, \
		.vq_queued_cnt = 0, \
		.vq_free_cnt = n, \
	}, \
	} \
	/**< @hideinitializer */

/**
 * @brief Retrieve a pointer to the virtqueue structure declared with VQ_DECLARE().
 *
 * @param name	The name of the virtqueue structure.
 *
 * @return A pointer to the virtqueue structure.
 */
#define VIRTIO_MMIO_VQ_PTR(name) \
	(&__vq_wrapper_##name.vq) \
	/**< @hideinitializer */

#ifdef __cplusplus
}
#endif

#endif /*ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_*/

/**
 * @}
 */
