/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <devicetree.h>
#include <device.h>
#include <drivers/virtio/virtio.h>
#include <net/ethernet.h>

#define DT_DRV_COMPAT virtio_net

#define DEV_CFG(dev) ((struct virtio_net_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_net_data*)(dev->data))

#define VQIN_SIZE    4
#define VQOUT_SIZE   4

struct virtio_net_config {
    const struct device *bus;
    int vq_count;
    struct virtqueue **vqs;
};

struct virtio_net_data {
};


static const struct ethernet_api virtio_net_api = {
};


#define CREATE_VIRTIO_NET_DEVICE(inst) \
    VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    VQ_DECLARE(vq1_##inst, VQOUT_SIZE, 4096);\
    static struct virtqueue *vq_list_##inst[] = {VQ_PTR(vq0_##inst), VQ_PTR(vq1_##inst)};\
    static const struct virtio_net_config virtio_net_cfg_##inst = {\
        .bus = DEVICE_DT_GET(DT_BUS(DT_INST(inst, DT_DRV_COMPAT))),\
        .vq_count = 2,\
        .vqs = &vq_list_##inst[0],\
        };\
    static struct virtio_net_data virtio_net_data_##inst = {\
    };\
    ETH_NET_DEVICE_DT_DEFINE(inst,\
        virtio_net_init,\
        NULL,\
        &virtio_net_data_##inst,\
        &virtio_net_cfg_##inst,\
        CONFIG_ETH_INIT_PRIORITY,\
        &virtio_net_api,\
        1514);


static int virtio_net_init(const struct device *dev)
{
    uint32_t devid, features;

    printk("%s()\n",__FUNCTION__);


    __ASSERT(DEV_CFG(dev)->bus != NULL, "DEV_CFG(dev)->bus != NULL");
    if (!device_is_ready(DEV_CFG(dev)->bus))
        return -1;
    printk("bus %p\n", DEV_CFG(dev)->bus);
    devid = virtio_get_devid(DEV_CFG(dev)->bus);
    if (devid != VIRTIO_ID_NETWORK)
        {
        printk("Bad devid %08x\n", devid);
        return -1;
        }
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER);
    virtio_set_features(DEV_CFG(dev)->bus, 0/*VIRTIO_F_NOTIFY_ON_EMPTY*/);
    features =virtio_get_features(DEV_CFG(dev)->bus);
    printk("features: %08x\n", features);
#if 0 //TODO:
    DEV_DATA(dev)->chan0->vqin =
        virtio_setup_virtqueue(
            DEV_CFG(dev)->bus,
            0,
            DEV_CFG(dev)->vqs[0],
#if !defined(ALTERNATE_POLL)
            virtio_serial_vqin_cb,
            DEV_DATA(dev)->chan0
#else
            NULL,
            NULL
#endif
            );
    if (!DEV_DATA(dev)->chan0->vqin)
        return -1;

    DEV_DATA(dev)->chan0->vqout =
        virtio_setup_virtqueue(
            DEV_CFG(dev)->bus,
            1,
            DEV_CFG(dev)->vqs[1],
#if !defined(ALTERNATE_POLL)
            virtio_serial_vqout_cb,
            DEV_DATA(dev)->chan0
#else
            NULL,
            NULL
#endif
            );
    if (!DEV_DATA(dev)->chan0->vqout)
        return -1;
#endif

    virtio_register_device(DEV_CFG(dev)->bus, DEV_CFG(dev)->vq_count, DEV_CFG(dev)->vqs);
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER_OK);
//TODO:
//    virtqueue_notify(DEV_DATA(dev)->chan0->vqin);
//    virtqueue_notify(DEV_DATA(dev)->chan0->vqout);

    char buf[64];
    virtio_read_config(DEV_CFG(dev)->bus, 0, buf, 64);
    for (int i = 0;i < 64; i++)
        printk("%02x  ", buf[i]);
    printk("\n");
    return 0;
}

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_NET_DEVICE)
