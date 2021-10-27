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

#include <drivers/virtio/virtio.h>

int virtqueue_enqueue_buf(struct virtqueue *vq, void *cookie, int writable, char *buffer, unsigned int len)
{
    struct vq_desc_extra *dxp;
    uint16_t head_idx, idx;
    uint16_t avail_idx, avail_ring_idx;
    struct vring_desc *dp;

    __ASSERT(buffer, "NULL buffer");
    __ASSERT(vq, "NULL vq");
    __ASSERT(cookie, "NULL cookie");
    if (vq->vq_free_cnt == 0)
        return (ENOSPC);
    head_idx = vq->vq_desc_head_idx;
    dxp = &vq->vq_descx[head_idx];
    __ASSERT(dxp->cookie == NULL, "cookie already exists");
    dxp->cookie = cookie;
//idx = vq_ring_enqueue_segments(vq, vq->vq_ring.desc, head_idx, sg, readable, writable);

    idx = head_idx;
    dp = &vq->vq_ring->desc[idx];
    dp->addr = Z_MEM_PHYS_ADDR((uintptr_t)buffer);
    dp->len = len;
    dp->flags = writable?VRING_DESC_F_WRITE:0;
    idx = dp->next;

    vq->vq_desc_head_idx = idx;
    vq->vq_free_cnt--;
//vq_ring_update_avail(vq, head_idx);
    avail_idx = vq->vq_ring->avail->idx;
    avail_ring_idx = avail_idx & (vq->vq_nentries - 1);
    vq->vq_ring->avail->ring[avail_ring_idx] = head_idx;
    __DMB();
    vq->vq_ring->avail->idx = avail_idx + 1;
    vq->vq_queued_cnt++;
    return 0;
}

void *virtqueue_dequeue_buf(struct virtqueue *vq, uint32_t *len)
{
    struct vring_used_elem *uep;
    void *cookie;
    uint16_t used_idx, desc_idx;
    struct vring_desc *dp;

    if (vq->vq_used_cons_idx == vq->vq_ring->used->idx)
        return NULL;

    used_idx = vq->vq_used_cons_idx++ & (vq->vq_nentries - 1);
    uep = &vq->vq_ring->used->ring[used_idx];

    __DMB();
    desc_idx = (uint16_t) uep->id;
    if (len != NULL)
        *len = uep->len;
//vq_ring_free_chain(vq, desc_idx);
    dp = &vq->vq_ring->desc[desc_idx];
    vq->vq_free_cnt++;
    dp->next = vq->vq_desc_head_idx;
    vq->vq_desc_head_idx = desc_idx;

    cookie = vq->vq_descx[desc_idx].cookie;
    __ASSERT(cookie != NULL, "no cookie");
    vq->vq_descx[desc_idx].cookie = NULL;

    return cookie;
}

void virtqueue_init(const struct device *dev, unsigned int idx, struct virtqueue *vq, void (*cb)(void*), void *cb_arg)
{
    struct vring *ring = vq->vq_ring;
    struct vq_desc_extra *dxp;
    int i;

    __ASSERT(!(vq->vq_nentries&(vq->vq_nentries - 1)), "Bad queue size");
    vq->vq_dev = dev;
    vq->cb = cb;
    vq->cb_arg = cb_arg;
    vq->vq_queue_index = idx;

    for (i = 0; i < vq->vq_nentries; i++) {
        dxp = &vq->vq_descx[i];
        dxp->cookie = NULL;
        }
    for (i = 0; i < (vq->vq_nentries - 1); i++)
        ring->desc[i].next = i + 1;
    ring->desc[i].next = VQ_RING_DESC_CHAIN_END;

}
