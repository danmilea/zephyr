
/*
 * Copyright (c) 2021-2022 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/virtio/virtio.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>
#include <openamp/virtio_rng_drv.h>

#define DT_DRV_COMPAT virtio_rng
#define LOG_MODULE_NAME virtio_rng
LOG_LEVEL_SET(CONFIG_VIRTIO_RNG_LOG_LEVEL);

#define DEV_CFG(dev) ((struct virtio_rng_config*)(dev->config))

struct virtio_rng_config {
    const struct device *bus;
    LOG_INSTANCE_PTR_DECLARE(log);
    int vq_count;
    struct virtqueue **vqs;
};

static int vtio_rng_init(const struct device *dev)
{
    struct virtio_device *vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);
    const char *vq_names[] = {"rvq"};

    if (!device_is_ready(DEV_CFG(dev)->bus)) {
        return -1;
    }

    return virtio_rng_init(vdev, DEV_CFG(dev)->vqs, vq_names, DEV_CFG(dev)->vq_count);
}

static int vtio_rng_get_entropy_internal(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    struct virtio_device *vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);
    return virtio_rng_get_entropy(vdev, buffer, length);
}

static int vtio_rng_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    int key, ret;

    LOG_INST_DBG(DEV_CFG(dev)->log, "(%p, %d)\n", buffer, length);

    key = irq_lock();
    ret = vtio_rng_get_entropy_internal(dev, buffer, length);
    irq_unlock(key);
    return ret;
}

static int vtio_rng_get_entropy_isr(const struct device *dev, uint8_t *buffer, uint16_t length, uint32_t flags)
{
    int ret;

    LOG_INST_DBG(DEV_CFG(dev)->log, "(%p, %d, %08x)\n", buffer, length, flags);

    ret = vtio_rng_get_entropy_internal(dev, buffer, length);
    if (!ret) {
        ret = length;
    }

    return ret;
}

static const struct entropy_driver_api virtio_rng_api = {
    .get_entropy = vtio_rng_get_entropy,
    .get_entropy_isr = vtio_rng_get_entropy_isr,
};

#define CREATE_VIRTIO_RNG_DEVICE(inst) \
    LOG_INSTANCE_REGISTER(LOG_MODULE_NAME, inst, CONFIG_VIRTIO_RNG_LOG_LEVEL);\
    VIRTIO_MMIO_VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    static struct virtqueue *vq_list_##inst[] = {VIRTIO_MMIO_VQ_PTR(vq0_##inst)};\
    static const struct virtio_rng_config virtio_rng_cfg_##inst = {\
        .bus = DEVICE_DT_GET(DT_BUS(DT_INST(inst, DT_DRV_COMPAT))),\
        LOG_INSTANCE_PTR_INIT(log, LOG_MODULE_NAME, inst)\
        .vq_count = 1,\
        .vqs = &vq_list_##inst[0],\
        };\
    DEVICE_DT_INST_DEFINE(	inst,\
    vtio_rng_init,\
    NULL,\
    NULL,\
    &virtio_rng_cfg_##inst,\
    PRE_KERNEL_1,\
    CONFIG_ENTROPY_INIT_PRIORITY,\
    &virtio_rng_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_RNG_DEVICE)
