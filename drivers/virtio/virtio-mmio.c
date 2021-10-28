/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <devicetree.h>
#include <device.h>
#include <drivers/virtio/virtio.h>

#define DT_DRV_COMPAT virtio_mmio

#define MMIO_MAGIC_VALUE        0x000
#define MAGIC_VALUE ('v'|('i'<<8)|('r'<<16)|('t'<<24))
#define MMIO_VERSION            0x004
#define MMIO_DEVICE_ID          0x008
#define MMIO_VENDOR_ID          0x00C
#define MMIO_HOST_FEATURES      0x010
#define MMIO_HOST_FEATURES_SEL  0x014
#define MMIO_GUEST_FEATURES     0x020
#define MMIO_GUEST_FEATURES_SEL 0x024
#define MMIO_GUEST_PAGE_SIZE    0x028
#define MMIO_QUEUE_SEL          0x030
#define MMIO_QUEUE_NUM_MAX      0x034
#define MMIO_QUEUE_NUM          0x038
#define MMIO_QUEUE_ALIGN        0x03C
#define MMIO_QUEUE_PFN          0x040
#define MMIO_QUEUE_NOTIFY       0x050

#define MMIO_INTERRUPT_STATUS   0x60
#define MMIO_INTERRUPT_ACK      0x64

#define MMIO_STATUS             0x070
#define MMIO_CONFIG             0x100

struct virtio_mmio_config {
    DEVICE_MMIO_ROM;
    void (*irq_config)(const struct device *dev);
};

struct virtio_mmio_data {
    DEVICE_MMIO_RAM;
    uint32_t gfeatures;
    int vq_num;
    struct virtqueue **vqs;
};

#define DEV_CFG(dev) ((struct virtio_mmio_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_mmio_data*)(dev->data))
#define READ32(dev, offset) sys_read32(DEVICE_MMIO_GET(dev) + offset)
#define WRITE32(dev, offset, val) sys_write32(val, DEVICE_MMIO_GET(dev) + offset)
#define READ8(dev, offset) sys_read8(DEVICE_MMIO_GET(dev) + offset)
#define WRITE8(dev, offset, val) sys_write8(val, DEVICE_MMIO_GET(dev) + offset)


static int virtio_mmio_init(const struct device *dev);
static void virtio_mmio_isr(const struct device *dev);

static uint32_t virtio_mmio_get_devid(const struct device *dev);

static uint8_t virtio_mmio_get_status(const struct device *dev);
static void virtio_mmio_set_status(const struct device *dev, uint8_t status);
static uint32_t virtio_mmio_get_features(const struct device *dev);
static void virtio_mmio_set_features(const struct device *dev, uint32_t features);
static void virtio_mmio_read_config(const struct device *dev, uint32_t offset,
                                    void *dst, int length);
static void virtio_mmio_register_device(const struct device *dev, int, struct virtqueue**);
static struct virtqueue * virtio_mmio_setup_virtqueue(const struct device *dev, unsigned int idx,
                                        struct virtqueue *vq, void (*cb)(void *), void *cb_arg);
static void virtio_mmio_virtqueue_notify(const struct device *dev, struct virtqueue *vq);


static const struct virtio_driver_api virtio_mmio_api = {
  .get_devid = virtio_mmio_get_devid,
  .get_status = virtio_mmio_get_status,
  .set_status = virtio_mmio_set_status,
  .get_features = virtio_mmio_get_features,
  .set_features = virtio_mmio_set_features,
  .read_config = virtio_mmio_read_config,
  .register_device = virtio_mmio_register_device,
  .setup_queue = virtio_mmio_setup_virtqueue,
  .queue_notify = virtio_mmio_virtqueue_notify,
};

#define CREATE_VIRTIO_MMIO_DEVICE(inst)                              \
    static void irq_config_##inst(const struct device *dev)          \
    {\
        IRQ_CONNECT(DT_INST_IRQ(inst, irq),\
                    DT_INST_IRQ(inst, priority),\
                    virtio_mmio_isr,\
                    DEVICE_DT_INST_GET(inst),\
                    0);\
    irq_enable(DT_INST_IRQ(inst, irq));\
    }\
    static struct virtio_mmio_config virtio_mmio_cfg_##inst = {      \
        DEVICE_MMIO_ROM_INIT(DT_DRV_INST(inst)),                     \
        irq_config_##inst,                                           \
    };                                                               \
    static struct virtio_mmio_data virtio_mmio_data_##inst = {       \
    };                                                               \
    DEVICE_DT_INST_DEFINE(inst,                                      \
                          virtio_mmio_init,                          \
                          NULL,                                      \
                          &virtio_mmio_data_##inst,                  \
                          &virtio_mmio_cfg_##inst,                   \
                          /*POST_KERNEL*/PRE_KERNEL_1,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,        \
                          &virtio_mmio_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_MMIO_DEVICE)

static int virtio_mmio_init(const struct device *dev)
{
    uintptr_t reg;
    uint32_t magic, version, devid, vendor;
    DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
    reg = DEVICE_MMIO_GET(dev);
    printk("device %s @%p\n",dev->name, dev);
    printk("iobase %lx\n",reg);

    magic = READ32(dev, MMIO_MAGIC_VALUE);
    if (magic != MAGIC_VALUE)
      {
      printk("Bad magic value %08x\n",magic);
      return -1;
      }
    version = READ32(dev, MMIO_VERSION);
    if (version != 1)
      {
      printk("Bad version %08x\n", version);
      return -1;
      }
    devid = virtio_get_devid(dev);
    if (devid == 0)
      {
      /* Placeholder */
      return -1;
      }
    vendor = READ32(dev, MMIO_VENDOR_ID);
    printk("VIRTIO %08x:%08x\n", vendor, devid);
    virtio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);
    WRITE32(dev, MMIO_GUEST_PAGE_SIZE, 4096);
    DEV_CFG(dev)->irq_config(dev);

    return 0;
}

static uint32_t virtio_mmio_get_devid(const struct device *dev)
{
  return READ32(dev, MMIO_DEVICE_ID);
}

static uint8_t virtio_mmio_get_status(const struct device *dev)
{
  return READ32(dev, MMIO_STATUS);
}
static void virtio_mmio_set_status(const struct device *dev, uint8_t status)
{
  WRITE32(dev, MMIO_STATUS, status);
  /* maybe notify(something,something) */
}
static uint32_t virtio_mmio_get_features(const struct device *dev)
{
  uint32_t hfeatures;
  hfeatures = READ32(dev, MMIO_HOST_FEATURES);
  return hfeatures & DEV_DATA(dev)->gfeatures;
}

/* this is more like negotiate_features */
static void virtio_mmio_set_features(const struct device *dev, uint32_t features)
{
  uint32_t hfeatures;
  hfeatures = READ32(dev, MMIO_HOST_FEATURES);
  features &= hfeatures;
  WRITE32(dev, MMIO_GUEST_FEATURES, features);
  DEV_DATA(dev)->gfeatures = features;
  /*virtio->notify(something,something)*/
}

static void virtio_mmio_read_config(const struct device *dev, uint32_t offset,
                                    void *dst, int length)
{
    int i;
    uint8_t *d = dst;
    for (i = 0 ; i < length; i++)
        d[i] = READ8(dev, MMIO_CONFIG + i);
}

static void virtio_mmio_register_device(const struct device *dev, int vq_num, struct virtqueue** vqs)
{
    DEV_DATA(dev)->vq_num = vq_num;
    DEV_DATA(dev)->vqs = vqs;
}


static struct virtqueue* virtio_mmio_setup_virtqueue(const struct device *dev,
    unsigned int idx,
    struct virtqueue *vq,
    void (*cb)(void*),
    void *cb_arg)
{
  uint32_t maxq;

  virtqueue_init(dev, idx, vq, cb, cb_arg);
  WRITE32(dev, MMIO_QUEUE_SEL, idx);
  maxq = READ32(dev, MMIO_QUEUE_NUM_MAX);
  __ASSERT(maxq !=0, "Queue not available");
  __ASSERT(maxq >= vq->vq_nentries, "Queue too small, vring won't fit");
  WRITE32(dev, MMIO_QUEUE_NUM, vq->vq_nentries);
  WRITE32(dev, MMIO_QUEUE_ALIGN, 4096);
  WRITE32(dev, MMIO_QUEUE_PFN, ((uintptr_t)Z_MEM_PHYS_ADDR((char*)vq->vq_ring->desc))/4096 );
  return vq;
}

static void virtio_mmio_virtqueue_notify(const struct device *dev, struct virtqueue *vq)
{

  __ASSERT(vq, "NULL vq");
  WRITE32(dev, MMIO_QUEUE_NOTIFY, vq->vq_queue_index);
}

static void virtio_mmio_isr(const struct device *dev)
{
    uint32_t isr=READ32(dev, MMIO_INTERRUPT_STATUS);
    struct virtqueue *vq;
    int i;

    if (isr & 1)
        {
        for (i = 0; i < DEV_DATA(dev)->vq_num; i++)
            {
            vq = DEV_DATA(dev)->vqs[i];
            if (vq->cb)
              vq->cb(vq->cb_arg);
            }
        }
    if (isr & 2)
        {
        printk("isr %p %08x CONFIG. Now what?\n", dev, isr);
        }
    WRITE32(dev, MMIO_INTERRUPT_ACK, isr);
}
