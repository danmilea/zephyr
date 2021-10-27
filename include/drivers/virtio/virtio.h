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

#ifndef ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_
#define ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_

#include <device.h>
#include "virtio_config.h"
#include "virtio_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vring;
struct virtqueue {
    const struct device *vq_dev;
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

#define VRING_DECLARE(name, n, align) \
/* Not sure if vrings need to be 4096 - aligned */\
char __vrbuf_##name [VRING_SIZE(n, align)] __aligned(4096); \
struct vring __vring_##name = {\
    .desc = (void*)__vrbuf_##name,\
    .avail = (void*)((unsigned long)__vrbuf_##name + n*sizeof(struct vring_desc)),\
    .used = (void*)((unsigned long)__vrbuf_##name + ((n*sizeof(struct vring_desc) +\
        (n + 1)* sizeof(uint16_t) + align - 1) &~(align - 1))),\
};

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
    };
#define VQ_PTR(name) &__vq_wrapper_##name.vq

typedef uint32_t (*virt_get_devid)(const struct device*);
typedef uint8_t (*virt_get_status)(const struct device*);
typedef void (*virt_set_status)(const struct device*, uint8_t);
/* actually, should be uint64_t */
typedef uint32_t (*virt_get_features)(const struct device *dev);
typedef void (*virt_set_features)(const struct device *dev, uint32_t);
typedef void (*virt_register_device)(const struct device *dev, int, struct virtqueue**);
typedef struct virtqueue * (*virt_setup_queue)(const struct device *dev, unsigned int, struct virtqueue*, void(*)(void*), void*);
typedef void (*virt_queue_notify)(const struct device *dev, struct virtqueue*);
__subsystem struct virtio_driver_api {
    virt_get_devid get_devid;
    virt_get_status get_status;
    virt_set_status set_status;
    virt_get_features get_features;
    virt_set_features set_features;
    virt_register_device register_device;
    virt_setup_queue setup_queue;
    virt_queue_notify queue_notify;
    
};

static inline uint32_t virtio_get_devid(const struct device *dev)
{
    return((struct virtio_driver_api*)dev->api)->get_devid(dev);
}

static inline uint8_t virtio_get_status(const struct device *dev)
{
    return((struct virtio_driver_api*)dev->api)->get_status(dev);
}

static inline void virtio_set_status(const struct device *dev, uint8_t status)
{
    ((struct virtio_driver_api*)dev->api)->set_status(dev, status);
}

static inline uint32_t virtio_get_features(const struct device *dev)
{
    return((struct virtio_driver_api*)dev->api)->get_features(dev);
}

static inline void virtio_set_features(const struct device *dev, uint32_t features)
{
    ((struct virtio_driver_api*)dev->api)->set_features(dev, features);
}


static inline void virtio_register_device(const struct device *dev, int vq_num, struct virtqueue **vqs)
{
    ((struct virtio_driver_api*)dev->api)->register_device(dev, vq_num, vqs);
}

static inline struct virtqueue* virtio_setup_virtqueue(const struct device *dev, unsigned int idx, struct virtqueue* vq, void (*cb)(void *), void *cb_arg)
{
    return ((struct virtio_driver_api*)dev->api)->setup_queue(dev, idx, vq, cb, cb_arg);
}

static inline void virtqueue_notify(struct virtqueue *vq)
{
    ((struct virtio_driver_api*)vq->vq_dev->api)->queue_notify(vq->vq_dev, vq);
}

extern int virtqueue_enqueue_buf(struct virtqueue *vq, void *cookie, int writable, char *buffer, unsigned int len);

extern void *virtqueue_dequeue_buf(struct virtqueue *vq, uint32_t *len);

static inline int virtqueue_empty(struct virtqueue *vq)
{
    return (vq->vq_nentries == vq->vq_free_cnt);
}

static inline int virtqueue_full(struct virtqueue *vq)
{
    return (vq->vq_free_cnt == 0);
}

extern void virtqueue_init(const struct device *dev, unsigned int idx, struct virtqueue *vq, void (*cb)(void *), void *cb_arg);

#ifdef __cplusplus
}
#endif

#endif /*ZEPHYR_INCLUDE_DRIVERS_DRIVERS_VIRTIO_H_*/