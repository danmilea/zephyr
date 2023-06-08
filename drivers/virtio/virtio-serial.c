/*
 * Copyright (c) 2021-2022 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/virtio/virtio.h>
#include <zephyr/drivers/uart.h>
#include <openamp/virtio_serial_drv.h>

#define SERIAL_PRIORITY    PRE_KERNEL_1
#define DT_DRV_COMPAT virtio_serial

#define DEV_CFG(dev) ((struct virtio_serial_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_serial_data*)(dev->data))

struct virtio_serial_config {
    const struct device *bus;
    int vq_count;
    struct virtqueue **vqs;
};

static int vtio_serial_poll_in(const struct device *dev, unsigned char *p_char);
static void vtio_serial_poll_out(const struct device *dev, unsigned char out_char);

struct uart_driver_api virtio_serial_api = {
    .poll_in = vtio_serial_poll_in,
    .poll_out = vtio_serial_poll_out,
};

#define CREATE_VIRTIO_SERIAL_DEVICE(inst) \
    VIRTIO_MMIO_VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    VIRTIO_MMIO_VQ_DECLARE(vq1_##inst, VQOUT_SIZE, 4096);\
    struct virtqueue *vq_list_##inst[] = {VIRTIO_MMIO_VQ_PTR(vq0_##inst), VIRTIO_MMIO_VQ_PTR(vq1_##inst)};\
    static struct virtio_serial_chan __chan0__##inst = {\
        .tx_inuse = ATOMIC_INIT(0),\
        .rx_inuse = ATOMIC_INIT(0),\
        .rxpoll_active = false,\
        .txpoll_active = false,\
    };\
    static const struct virtio_serial_config virtio_serial_cfg_##inst = {\
        .bus = DEVICE_DT_GET(DT_BUS(DT_INST(inst, DT_DRV_COMPAT))),\
        .vq_count = 2,\
        .vqs = &vq_list_##inst[0],\
        };\
    static struct virtio_serial_data virtio_serial_data_##inst = {\
    .chan0 = &__chan0__##inst,\
    };\
    DEVICE_DT_INST_DEFINE(	inst,\
    vtio_serial_init,\
    NULL,\
    &virtio_serial_data_##inst,\
    &virtio_serial_cfg_##inst,\
    SERIAL_PRIORITY,\
    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,\
    &virtio_serial_api);

static int vtio_serial_init(const struct device *dev)
{
    __ASSERT(DEV_CFG(dev)->bus != NULL, "DEV_CFG(dev)->bus != NULL");
    if (!device_is_ready(DEV_CFG(dev)->bus))
        return -1;

    struct virtio_device *vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);
    char *vq_names[] = {"vqin", "vqout"};
    struct virtio_serial_data *dev_data = DEV_DATA(dev);
    void (*cbs[])(void *) = {NULL, NULL};
    void *cb_args[] = {NULL, NULL};

    if (!vdev || !vdev->priv /*|| !(((struct virtio_mmio_device *)(vdev->priv))->user_data)*/ ) {
        return -1;
    }

    vdev->priv = dev_data;
    return virtio_serial_init(vdev, DEV_CFG(dev)->vqs, vq_names, cbs, cb_args, DEV_CFG(dev)->vq_count);
}

static int vtio_serial_poll_in(const struct device *dev, unsigned char *p_char)
{
    struct virtio_device *vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);
    return virtio_serial_poll_in(vdev, p_char);
}

static void vtio_serial_poll_out(const struct device *dev, unsigned char out_char)
{
    struct virtio_device *vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);
    virtio_serial_poll_out(vdev, out_char);
}

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_SERIAL_DEVICE)
