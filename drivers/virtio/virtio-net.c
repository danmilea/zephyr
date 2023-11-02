/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/virtio/virtio.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/sys/slist.h>
#include <zephyr/logging/log.h>
#include <openamp/virtio_net_drv.h>

#define DT_DRV_COMPAT virtio_net

#define LOG_MODULE_NAME virtio_net
LOG_LEVEL_SET(CONFIG_VIRTIO_NET_LOG_LEVEL);

#define DEV_CFG(dev) ((struct vtio_net_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_net_data*)(dev->data))

struct vtio_net_config {
    const struct device *bus;
    LOG_INSTANCE_PTR_DECLARE(log);
    int vq_count;
    struct virtqueue **vqs;
};

static void vtio_net_iface_init(struct net_if *iface);
static int vtio_net_send(const struct device *dev, struct net_pkt *pkt);

void pkt_in_cb(uint8_t *data, int length, void *arg) {
    struct net_if *iface = *((struct net_if **)arg);
    if (iface) {
        struct net_pkt *pkt;
        pkt = net_pkt_rx_alloc_with_buffer(iface, length, AF_UNSPEC, 0, K_NO_WAIT);
        if (pkt != NULL) {
            net_pkt_write(pkt, data, length);
            if (net_recv_data(iface, pkt) < 0) {
                net_pkt_unref(pkt);
            }
        }
    }
}

static const struct ethernet_api virtio_net_api = {
    .iface_api.init = vtio_net_iface_init,
    .send = vtio_net_send,
};

#define CREATE_VIRTIO_NET_RXBUFS(inst) \
    static struct virtio_net_rx_pkt rxbuf_##inst[RXDESC_COUNT];
#define SET_VIRTIO_NET_RXBUFS(inst) \
        .rxbuf = rxbuf_##inst,

#define CREATE_VIRTIO_NET_DEVICE(inst) \
    LOG_INSTANCE_REGISTER(LOG_MODULE_NAME, inst, CONFIG_VIRTIO_NET_LOG_LEVEL);\
    CREATE_VIRTIO_NET_RXBUFS(inst);\
    static struct virtio_net_rx_desc rxdesc_##inst[RXDESC_COUNT];\
    static struct virtio_net_tx_pkt txbuf_##inst[TXDESC_COUNT];\
    static struct virtio_net_tx_desc txdesc_##inst[TXDESC_COUNT];\
    VIRTIO_MMIO_VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    VIRTIO_MMIO_VQ_DECLARE(vq1_##inst, VQOUT_SIZE, 4096);\
    static struct virtqueue *vq_list_##inst[] = {VIRTIO_MMIO_VQ_PTR(vq0_##inst), VIRTIO_MMIO_VQ_PTR(vq1_##inst)};\
    static const struct vtio_net_config virtio_net_cfg_##inst = {\
        .bus = DEVICE_DT_GET(DT_BUS(DT_INST(inst, DT_DRV_COMPAT))),\
        LOG_INSTANCE_PTR_INIT(log, LOG_MODULE_NAME, inst)\
        .vq_count = 2,\
        .vqs = &vq_list_##inst[0],\
        };\
    static struct virtio_net_data virtio_net_data_##inst = {\
        .rxbuf = rxbuf_##inst, \
        .rxdesc = rxdesc_##inst,\
        .txbuf = txbuf_##inst,\
        .txdesc = txdesc_##inst,\
    };\
    ETH_NET_DEVICE_DT_INST_DEFINE(inst,\
        vtio_net_init,\
        NULL,\
        &virtio_net_data_##inst,\
        &virtio_net_cfg_##inst,\
        CONFIG_ETH_INIT_PRIORITY,\
        &virtio_net_api,\
        NET_ETH_MTU);

static int vtio_net_init(const struct device *dev)
{
    struct virtio_device *vdev = NULL;
    const char *vq_names[] = {"vqin", "vqout"};
    struct virtio_net_data *data = DEV_DATA(dev);
    void (*cbs[])(void *) = {virtio_net_vqin_cb, virtio_net_vqout_cb};
    void *cb_args[2];

    LOG_INST_DBG(DEV_CFG(dev)->log, "(%p)\n", dev);
    __ASSERT(DEV_CFG(dev)->bus != NULL, "DEV_CFG(dev)->bus != NULL");

    if (!device_is_ready(DEV_CFG(dev)->bus)) {
        return -1;
    }

    vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);

    if (!vdev || !vdev->priv || !(((struct virtio_mmio_device *)(vdev->priv))->user_data)) {
        return -1;
    }

    cb_args[0] = vdev;
    cb_args[1] = data;

    data->net_pkt_in_cb = pkt_in_cb;
    data->net_pkt_in_cb_arg = &data->iface;
    vdev->priv = data;

    return virtio_net_init(vdev, DEV_CFG(dev)->vqs, vq_names, cbs, cb_args, DEV_CFG(dev)->vq_count);
}

static void vtio_net_iface_init(struct net_if *iface)
{
    const struct device *dev = net_if_get_device(iface);

    LOG_INST_DBG(DEV_CFG(dev)->log, "(%p)\n", iface);

    DEV_DATA(dev)->iface = iface;
    net_if_set_link_addr(iface, DEV_DATA(dev)->mac_addr, 6, NET_LINK_ETHERNET);
    ethernet_init(iface);
    //net_if_flag_set(iface, NET_IF_NO_AUTO_START);
}

static int vtio_net_send(const struct device *dev, struct net_pkt *pkt)
{
    struct virtio_device *vdev = virtio_get_vmmio_dev(DEV_CFG(dev)->bus);
    uint8_t *data = k_malloc(NET_ETH_MAX_FRAME_SIZE);
    uint16_t total_len;
    int ret = 0;

    if (!data) {
        LOG_INST_WRN(DEV_CFG(dev)->log, "Failed to allocate data buffer");
        return -ENOMEM;
    }

    total_len = net_pkt_get_len(pkt);
    memset(data, 0, NET_ETH_MAX_FRAME_SIZE);

    if (net_pkt_read(pkt, data, total_len)) {
        LOG_INST_WRN(DEV_CFG(dev)->log, "Failed to read packet into buffer");
        k_free(data);
        return -EIO;
    }

    ret = virtio_net_send(vdev, data, total_len);
    k_free(data);

    return ret;
}

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_NET_DEVICE)
