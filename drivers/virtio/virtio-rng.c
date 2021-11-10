
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
#define printk(...) do {} while(0)

#define VQIN_SIZE 2

#define DEV_CFG(dev) ((struct virtio_rng_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_rng_data*)(dev->data))

struct virtio_rng_config {
    const struct device *bus;
    int vq_count;
    struct virtqueue **vqs;
};

struct virtio_rng_data {
    struct virtqueue *vqin;
};

static int virtio_rng_init(const struct device *dev)
{
    uint32_t devid, features;

    printk("%s()\n",__FUNCTION__);

    __ASSERT(DEV_CFG(dev)->bus != NULL, "DEV_CFG(dev)->bus != NULL");
    if (!device_is_ready(DEV_CFG(dev)->bus))
        return -1;
    printk("bus %p\n", DEV_CFG(dev)->bus);
    devid = virtio_get_devid(DEV_CFG(dev)->bus);
    if (devid != VIRTIO_ID_ENTROPY)
        {
        printk("Bad devid %08x\n", devid);
        return -1;
        }
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER);
    virtio_set_features(DEV_CFG(dev)->bus, 0/*VIRTIO_F_NOTIFY_ON_EMPTY*/);
    features =virtio_get_features(DEV_CFG(dev)->bus);
    printk("features: %08x\n", features);

    DEV_DATA(dev)->vqin =
        virtio_setup_virtqueue(
            DEV_CFG(dev)->bus,
            0,
            DEV_CFG(dev)->vqs[0],
            NULL,
            NULL
            );
    if (!DEV_DATA(dev)->vqin)
        return -1;
    virtio_register_device(DEV_CFG(dev)->bus, DEV_CFG(dev)->vq_count, DEV_CFG(dev)->vqs);
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER_OK);

    virtqueue_notify(DEV_DATA(dev)->vqin);

    return 0;
}

static int virtio_rng_get_entropy_internal(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    void *cookie;
    /*Not supposed to happen*/
    if (virtqueue_enqueue_buf(DEV_DATA(dev)->vqin, buffer, 1, buffer, length))
	    return -EIO;
    virtqueue_notify(DEV_DATA(dev)->vqin);
    do {
	    cookie = virtqueue_dequeue_buf(DEV_DATA(dev)->vqin, NULL);
	    /* TODO: yield or something if not in isr context. might not work, as we're irq locked when called from task context */
    } while (!cookie);
    __ASSERT(cookie == buffer, "Got the wrong cookie");
    return 0;
}

static int virtio_rng_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    int key, ret;
    key = irq_lock();
    ret = virtio_rng_get_entropy_internal(dev, buffer, length);
    irq_unlock(key);
    return ret;
}

static int virtio_rng_get_entropy_isr(const struct device *dev, uint8_t *buffer, uint16_t length, uint32_t flags)
{
    int key, ret;
    ret = virtio_rng_get_entropy_internal(dev, buffer, length);
    if (!ret)
        return length;
    return ret;
}

static const struct entropy_driver_api virtio_rng_api = {
    .get_entropy = virtio_rng_get_entropy,
    .get_entropy_isr = virtio_rng_get_entropy_isr,
};

#define CREATE_VIRTIO_RNG_DEVICE(inst) \
    VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    static struct virtqueue *vq_list_##inst[] = {VQ_PTR(vq0_##inst)};\
    static const struct virtio_rng_config virtio_rng_cfg_##inst = {\
        .bus = DEVICE_DT_GET(DT_BUS(DT_INST(inst, DT_DRV_COMPAT))),\
        .vq_count = 1,\
        .vqs = &vq_list_##inst[0],\
        };\
    static struct virtio_rng_data virtio_rng_data_##inst = {\
        };\
    DEVICE_DT_INST_DEFINE(	inst,\
    virtio_rng_init,\
    NULL,\
    &virtio_rng_data_##inst,\
    &virtio_rng_cfg_##inst,\
    PRE_KERNEL_1,\
    CONFIG_ENTROPY_INIT_PRIORITY,\
    &virtio_rng_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_RNG_DEVICE)
