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
#include <sys/slist.h>

#define DT_DRV_COMPAT virtio_net

#define DEV_CFG(dev) ((struct virtio_net_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_net_data*)(dev->data))

#define VQIN_SIZE    4
#define VQOUT_SIZE   4

#define VIRTIO_NET_F_MAC BIT(5)
#define VIRTIO_NET_F_MRG_RXBUF BIT(15)

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
};

struct virtio_net_pkt {
    struct virtio_net_hdr hdr;
    uint8_t pkt[NET_ETH_MAX_FRAME_SIZE];
};

struct virtio_net_desc {
    sys_snode_t node;
    struct virtio_net_pkt *pkt;
    uint8_t *data;
};

struct virtio_net_config {
    const struct device *bus;
    int vq_count;
    struct virtqueue **vqs;
    struct virtio_net_pkt *txbuf;
    struct virtio_net_desc *txdesc;
};

struct virtio_net_data {
    uint8_t mac_addr[6];
    struct virtqueue *vqin, *vqout;
    int hdrsize;
    sys_slist_t tx_free_list;
};

static void virtio_net_iface_init(struct net_if *iface);
static int virtio_net_send(const struct device *dev, struct net_pkt *pkt);

static const struct ethernet_api virtio_net_api = {
    .iface_api.init = virtio_net_iface_init,
    .send = virtio_net_send,
};


#define CREATE_VIRTIO_NET_DEVICE(inst) \
    static struct virtio_net_pkt txbuf_##inst[VQOUT_SIZE];\
    static struct virtio_net_desc txdesc_##inst[VQOUT_SIZE];\
    VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    VQ_DECLARE(vq1_##inst, VQOUT_SIZE, 4096);\
    static struct virtqueue *vq_list_##inst[] = {VQ_PTR(vq0_##inst), VQ_PTR(vq1_##inst)};\
    static const struct virtio_net_config virtio_net_cfg_##inst = {\
        .bus = DEVICE_DT_GET(DT_BUS(DT_INST(inst, DT_DRV_COMPAT))),\
        .vq_count = 2,\
        .vqs = &vq_list_##inst[0],\
        .txbuf = txbuf_##inst,\
        .txdesc = txdesc_##inst,\
        };\
    static struct virtio_net_data virtio_net_data_##inst = {\
    };\
    ETH_NET_DEVICE_DT_INST_DEFINE(inst,\
        virtio_net_init,\
        NULL,\
        &virtio_net_data_##inst,\
        &virtio_net_cfg_##inst,\
        CONFIG_ETH_INIT_PRIORITY,\
        &virtio_net_api,\
        NET_ETH_MTU);

static int virtio_net_init(const struct device *dev)
{
    uint32_t devid, features;
    int i;

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
    virtio_set_features(DEV_CFG(dev)->bus, VIRTIO_NET_F_MAC/*VIRTIO_F_NOTIFY_ON_EMPTY*/);
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

    DEV_DATA(dev)->vqout =
        virtio_setup_virtqueue(
            DEV_CFG(dev)->bus,
            1,
            DEV_CFG(dev)->vqs[1],
            NULL,
            NULL
            );
    if (!DEV_DATA(dev)->vqout)
        return -1;

    virtio_register_device(DEV_CFG(dev)->bus, DEV_CFG(dev)->vq_count, DEV_CFG(dev)->vqs);
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER_OK);

    DEV_DATA(dev)->hdrsize = sizeof(struct virtio_net_hdr);
    if (!(features & VIRTIO_NET_F_MRG_RXBUF))
        DEV_DATA(dev)->hdrsize -= 2;

    sys_slist_init(&DEV_DATA(dev)->tx_free_list);
    for (i = 0; i < VQOUT_SIZE; i++)
        {
        printk("tx %d at %p\n",i,&DEV_CFG(dev)->txdesc[i]);
        DEV_CFG(dev)->txdesc[i].pkt = &DEV_CFG(dev)->txbuf[i];
        if (features & VIRTIO_NET_F_MRG_RXBUF)
            DEV_CFG(dev)->txdesc[i].data = DEV_CFG(dev)->txbuf[i].pkt;
        else
            DEV_CFG(dev)->txdesc[i].data = (uint8_t*)&DEV_CFG(dev)->txbuf[i].hdr.num_buffers;
        sys_slist_append(&DEV_DATA(dev)->tx_free_list, &DEV_CFG(dev)->txdesc[i].node);
        }
//TODO:
    virtqueue_notify(DEV_DATA(dev)->vqin);
    virtqueue_notify(DEV_DATA(dev)->vqout);

    char buf[64];
    virtio_read_config(DEV_CFG(dev)->bus, 0, buf, 64);
    for (int i = 0;i < 64; i++)
        printk("%02x  ", buf[i]);
    printk("\n");
    if (VIRTIO_NET_F_MAC & features)
        virtio_read_config(DEV_CFG(dev)->bus, 0, DEV_DATA(dev)->mac_addr, 6);
    else
        {
        __ASSERT(0, "should generate a MAC address");
        }

    return 0;
}

static void virtio_net_iface_init(struct net_if *iface)
{
    const struct device *dev = net_if_get_device(iface);
    printk("%s()\n",__FUNCTION__);

    net_if_set_link_addr(iface, DEV_DATA(dev)->mac_addr, 6, NET_LINK_ETHERNET);
    ethernet_init(iface);
    //net_if_flag_set(iface, NET_IF_NO_AUTO_START);
}

static int virtio_net_send(const struct device *dev, struct net_pkt *pkt)
{
    sys_snode_t *node;
    struct virtio_net_desc *desc, *ret;
    uint16_t total_len;

    printk("%s()\n",__FUNCTION__);
    total_len = net_pkt_get_len(pkt);
    if (total_len > NET_ETH_MAX_FRAME_SIZE)
        {
        return -EIO;
        }

    //TODO: locking
    if (virtqueue_full(DEV_DATA(dev)->vqout))
        return -EIO;
    /*
     * doing SYS_SLIST_CONTAINER(sys_slist_get(...),...) is a bad idea
     * first parameter of SYS_SLIST_CONTAINER() is evaluated twice
     */
    node = sys_slist_get(&DEV_DATA(dev)->tx_free_list);
    desc = SYS_SLIST_CONTAINER(node, desc, node);
    if (!desc)
        return -EIO;
    memset(&desc->pkt->hdr, 0, sizeof(desc->pkt->hdr));
    if (net_pkt_read(pkt, desc->data , total_len))
        {
            printk("Failed to read packet into buffer");
            sys_slist_append(&DEV_DATA(dev)->tx_free_list, &desc->node);
            return -EIO;
        }
    virtqueue_enqueue_buf(DEV_DATA(dev)->vqout, (void*)desc, 0, (char*)desc->pkt, total_len + DEV_DATA(dev)->hdrsize);
    virtqueue_notify(DEV_DATA(dev)->vqout);
    //polling for now
    do {
        ret = virtqueue_dequeue_buf(DEV_DATA(dev)->vqout, NULL);
        } while(!ret);
    __ASSERT(ret == desc, "whose buffer is this?");
    sys_slist_append(&DEV_DATA(dev)->tx_free_list, &ret->node);
    return 0;
}

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_NET_DEVICE)
