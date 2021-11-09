
/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <devicetree.h>
#include <device.h>
#include <drivers/virtio/virtio.h>
#include <drivers/entropy.h>

#define DT_DRV_COMPAT virtio_rng

static int virtio_rng_init(const struct device *dev)
{
    uint32_t devid, features;

    printk("%s()\n",__FUNCTION__);

    return 0;
}

static int virtio_rng_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    return 0;
}

static const struct entropy_driver_api virtio_rng_api = {
        .get_entropy = virtio_rng_get_entropy
};

#define CREATE_VIRTIO_RNG_DEVICE(inst) \
    DEVICE_DT_INST_DEFINE(	inst,\
    virtio_rng_init,\
    NULL,\
    NULL/*data*/,\
    NULL/*cfg*/,\
    PRE_KERNEL_1,\
    CONFIG_ENTROPY_INIT_PRIORITY,\
    &virtio_rng_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_RNG_DEVICE)
