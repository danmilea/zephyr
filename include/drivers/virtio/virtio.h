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

#include <device.h>
#include "virtio_config.h"
#include "virtio_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal use only, skip these in public documentation.
 */

struct vring;
struct virtqueue {
    const struct device *vq_dev;
#ifdef RSLD
    sys_slist_t bb_list;
#endif
    uint16_t vq_queue_index;
    uint16_t vq_nentries;
    struct vring *vq_ring;
    void (*cb)(void *);
    void *cb_arg;

    uint16_t vq_free_cnt;
    uint16_t vq_queued_cnt;
    /*
     * Head of the free chain in the descriptor table. If
     * there are no free descriptors, this will be set to
     * VQ_RING_DESC_CHAIN_END.
     */
    uint16_t vq_desc_head_idx;
    /*
     * Last consumed descriptor in the used table,
     * trails vq_ring.used->idx.
     */
    uint16_t vq_used_cons_idx;
    /* Not doing indirect descriptors */
    struct vq_desc_extra {
        void *cookie;
    } vq_descx[0];
};

struct vring_desc {
    uint64_t addr;
    uint32_t len;
#define VRING_DESC_F_NEXT        1
#define VRING_DESC_F_WRITE       2
#define VRING_DESC_F_INDIRECT    4
    uint16_t flags;
    uint16_t next;
} __packed;

struct vring_avail {
#define VRING_AVAIL_F_NO_INTERRUPT    1
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[0];
} __packed;

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __packed;

struct vring_used {
#define VRING_USED_F_NO_NOTIFY        1
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[0];
} __packed;

struct vring {
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
};


/*
 * The maximum virtqueue size is 2^15. Use that value as the end of
 * descriptor chain terminator since it will never be a valid index
 * in the descriptor table. This is used to verify we are correctly
 * handling vq_free_cnt.
 */
#define VQ_RING_DESC_CHAIN_END 32768

#define VRING_SIZE(n, align) \
    ( \
        ( \
        sizeof(struct vring_desc) * n + \
        sizeof(struct vring_avail) + \
        sizeof(uint16_t) *(n + 1) + \
        align - 1 \
        ) \
        &~ (align - 1) \
    ) + \
    sizeof(struct vring_used) + \
    sizeof(struct vring_used_elem) * n + sizeof(uint16_t)

#ifndef RSLD
#define VRING_DECLARE(name, n, align) \
/* Not sure if vrings need to be 4096 - aligned */\
static char __vrbuf_##name [VRING_SIZE(n, align)] __aligned(4096); \
static struct vring __vring_##name = {\
    .desc = (void*)__vrbuf_##name,\
    .avail = (void*)((unsigned long)__vrbuf_##name + n*sizeof(struct vring_desc)),\
    .used = (void*)((unsigned long)__vrbuf_##name + ((n*sizeof(struct vring_desc) +\
        (n + 1)* sizeof(uint16_t) + align - 1) &~(align - 1))),\
};
#else /* vring in its own section */
#define VRING_DECLARE(name, n, align) \
/* Not sure if vrings need to be 4096 - aligned */\
static char __vrbuf_##name [VRING_SIZE(n, align)] __aligned(4096) __attribute((__section__(".shared.vring."#name))); \
static struct vring __vring_##name = {\
    .desc = (void*)__vrbuf_##name,\
    .avail = (void*)((unsigned long)__vrbuf_##name + n*sizeof(struct vring_desc)),\
    .used = (void*)((unsigned long)__vrbuf_##name + ((n*sizeof(struct vring_desc) +\
        (n + 1)* sizeof(uint16_t) + align - 1) &~(align - 1))),\
};
#endif /* vring in its own section */

/**
 * @endcond
 */

/** @brief Declare a virtqueue structure.
 *
 * @param[in] name The name of the virtqueue structure.
 * @param[in] n Size of the virtqueue. Must be a power of 2.
 * @param[in] align Memory alignment of the associated vring structures.
 * @note TODO: look into MMIO_QUEUE_ALIGN
 */

#define VQ_DECLARE(name, n, align) \
    VRING_DECLARE(name, n, align);\
    static struct {\
        struct virtqueue vq;\
        struct vq_desc_extra extra[n];\
    } __vq_wrapper_##name = {\
        .vq = {\
            .vq_nentries = n,\
            .vq_ring = &__vring_##name,\
            .vq_queued_cnt = 0,\
            .vq_free_cnt = n,\
        },\
    };\
    /**< @hideinitializer */

/** @brief Retrieve a pointer to the virtqueue structure declared with VQ_DECLARE().
 * @details Use this for creating the virtqueue array required by virtio_register_device().
 *
 * @param[in] name The name of the virtqueue structure.
 *
 * @return A pointer to the virtqueue structure.
 */

#define VQ_PTR(name) \
    &__vq_wrapper_##name.vq \
    /**< @hideinitializer */

typedef uint32_t (*virt_get_devid)(const struct device*);
typedef uint8_t (*virt_get_status)(const struct device*);
typedef void (*virt_set_status)(const struct device*, uint8_t);
/* actually, should be uint64_t */
typedef uint32_t (*virt_get_features)(const struct device *dev);
typedef void (*virt_set_features)(const struct device *dev, uint32_t);
typedef void (*virt_read_config)(const struct device *dev, uint32_t, void *, int);
typedef void (*virt_register_device)(const struct device *dev, int, struct virtqueue**);
typedef struct virtqueue * (*virt_setup_queue)(const struct device *dev, unsigned int, struct virtqueue*, void(*)(void*), void*);
typedef void (*virt_queue_notify)(const struct device *dev, struct virtqueue*);
__subsystem struct virtio_driver_api {
    virt_get_devid get_devid;
    virt_get_status get_status;
    virt_set_status set_status;
    virt_get_features get_features;
    virt_set_features set_features;
    virt_read_config read_config;
    virt_register_device register_device;
    virt_setup_queue setup_queue;
    virt_queue_notify queue_notify;
    
};


/**
 * @brief Get device ID.
 *
 * @param[in] dev Pointer to device structure.
 *
 * @return Device ID value.
 */

static inline uint32_t virtio_get_devid(const struct device *dev)
{
    return((struct virtio_driver_api*)dev->api)->get_devid(dev);
}

/**
 * @brief Retrieve device status.
 *
 * @param[in] dev Pointer to device structure.
 *
 * @return status of the device.
 */

static inline uint8_t virtio_get_status(const struct device *dev)
{
    return((struct virtio_driver_api*)dev->api)->get_status(dev);
}


/**
 * @brief Set device status.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in] status Value to be set as device status.
 *
 * @return N/A.
 */

static inline void virtio_set_status(const struct device *dev, uint8_t status)
{
    ((struct virtio_driver_api*)dev->api)->set_status(dev, status);
}

/**
 * @brief Retrieve configuration data from the device.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in] offset Offset of the data within the configuration area.
 * @param[in] dst Address of the buffer that will hold the data.
 * @param[in] len Length of the data to be retrieved.
 *
 * @return N/A.
 */

static inline void virtio_read_config(const struct device *dev, uint32_t offset, void *dst, int len)
{
    ((struct virtio_driver_api*)dev->api)->read_config(dev, offset, dst, len);
}

/**
 * @brief Retrieve features supported by both the VIRTIO driver and the VIRTIO device.
 *
 * @param[in] dev Pointer to device structure.
 *
 * @return Features supported by both the driver and the device as a bitfield.
 */

static inline uint32_t virtio_get_features(const struct device *dev)
{
    return((struct virtio_driver_api*)dev->api)->get_features(dev);
}

/**
 * @brief Set features supported by the VIRTIO driver.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in] features Features supported by the driver as a bitfield.
 *
 * @return N/A.
 */

static inline void virtio_set_features(const struct device *dev, uint32_t features)
{
    ((struct virtio_driver_api*)dev->api)->set_features(dev, features);
}

/**
 * @brief Register a VIRTIO device with the VIRTIO stack.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in] vq_num Number of virtqueues the device uses.
 * @param[in] vqs Array of pointers to vthe virtqueues used by the device.
 *
 * @return N/A.
 */

static inline void virtio_register_device(const struct device *dev, int vq_num, struct virtqueue **vqs)
{
    ((struct virtio_driver_api*)dev->api)->register_device(dev, vq_num, vqs);
}

/**
 * @brief Setup a virtqueue structure.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in] idx Index of the virtqueue.
 * @param[in] vq Pointer to virtqueue structure.
 * @param[in] cb Pointer to virtqueue callback. Can be NULL.
 * @param[in] cb_arg Argument for the virtqueue callback.
 *
 * @return pointer to virtqueue structure.
 */

static inline struct virtqueue* virtio_setup_virtqueue(const struct device *dev, unsigned int idx, struct virtqueue* vq, void (*cb)(void *), void *cb_arg)
{
    return ((struct virtio_driver_api*)dev->api)->setup_queue(dev, idx, vq, cb, cb_arg);
}

/**
 * @brief Notify device of virtqueue activity.
 *
 * @param[in] vq Pointer to virtqueue structure.
 *
 * @return N/A.
 */

static inline void virtqueue_notify(struct virtqueue *vq)
{
    ((struct virtio_driver_api*)vq->vq_dev->api)->queue_notify(vq->vq_dev, vq);
}

/**
 * @brief Enqueues a single buffer on a virtqueue.
 *
 * @param[in] vq Pointer to virtqueue structure.
 * @param[in] cookie Pointer to be returned on dequeue. Cannot be NULL.
 * @param[in] writable Indicates if buffer is writable by device.
 * @param[in] buffer Pointer to buffer.
 * @param[in] len Length of the buffer.
 *
 * @return 0 on success.
 * @return -ENOSPC if queue is full.
 */

extern int virtqueue_enqueue_buf(struct virtqueue *vq, void *cookie, int writable, char *buffer, unsigned int len);

/**
 * @brief Dequeues a single buffer from a virtqueue.
 *
 * @param[in] vq Pointer to virtqueue structure.
 * @param[out] len Pointer to length of transferred data. Can be NULL. 
 *
 * @return cookie parameter to virtqueue_enqueue_buf() if a buffer is available.
 * @return NULL if there is no buffer available.
 */

extern void *virtqueue_dequeue_buf(struct virtqueue *vq, uint32_t *len);

/**
 * @brief Test if a virtqueue is empty.
 *
 * @param[in] vq Pointer to virtqueue structure.
 *
 * @return 1 if virtqueue is empty.
 * @return 0 if virtqueue is not empty.
 */

static inline int virtqueue_empty(struct virtqueue *vq)
{
    return (vq->vq_nentries == vq->vq_free_cnt);
}

/**
 * @brief Test if a virtqueue is full.
 *
 * @param[in] vq Pointer to virtqueue structure.
 *
 * @return 1 if virtqueue is full.
 * @return 0 if virtqueue is not full.
 */

static inline int virtqueue_full(struct virtqueue *vq)
{
    return (vq->vq_free_cnt == 0);
}

/**
 * @brief Initialize a virtqueue structure. Only callable from transport drivers.
 *
 * @details Do not call this from a regular device driver.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in] idx Index of the virtqueue.
 * @param[in] vq Pointer to virtqueue structure.
 * @param[in] cb Pointer to virtqueue callback. Can be NULL.
 * @param[in] cb_arg Argument for the virtqueue callback.
 *
 * @return N/A.
 */

extern void virtqueue_init(const struct device *dev, unsigned int idx, struct virtqueue *vq, void (*cb)(void *), void *cb_arg);

#ifdef __cplusplus
}
#endif

#endif /*ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_*/

/**
 * @}
 */
