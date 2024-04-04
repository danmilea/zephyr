/*
 * Copyright (c) 2021-2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/sys/mutex.h>
#include <openamp/virtio_mmio.h>
#include <openamp/virtio_mmio_hvl.h>
#include <stdio.h>
#include <string.h>

#define DT_DRV_COMPAT xlnx_zynqmp_ipi

/* Register offsets */
#define IPI_TRIG    0x00
#define IPI_OBS     0x04
#define IPI_ISR     0x10
#define IPI_IMR     0x14
#define IPI_IER     0x18
#define IPI_IDR     0x1C

#define READ32(dev, offset) sys_read32(DEVICE_MMIO_GET(dev) + offset)
#define WRITE32(dev, offset, val) sys_write32(val, DEVICE_MMIO_GET(dev) + offset)
#define HVL_CFG_ACK 0xAABBAABB

struct xlnx_ipi_config {
    DEVICE_MMIO_ROM;
    void (*irq_config)(const struct device *dev);
};

static int xlnx_ipi_init(const struct device *dev);
static void xlnx_ipi_isr(const struct device *dev);

static int xlnx_ipi_init(const struct device *dev)
{
    DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

    WRITE32(dev, IPI_IDR, 0xFFFFFFFF);
    WRITE32(dev, IPI_ISR, 0xFFFFFFFF);
    ((struct xlnx_ipi_config *)dev->config)->irq_config(dev);

    WRITE32(dev, IPI_IER, (1 << 24));
    return 0;
}

static void xlnx_ipi_isr(const struct device *dev)
{
    uint32_t isr, imr;
    uint32_t i;

    isr = READ32(dev, IPI_ISR);
    imr = READ32(dev, IPI_IMR);

    for (i = 0 ; i < 32; i++)
    {
        if (imr & (1 << i)) /* masked */
            continue;
        if (!(isr & (1 << i))) /* not active */
            continue;
        /* ACK interrupt */
        WRITE32(dev, IPI_ISR, (1 << i));
    }

#ifdef HVL_VIRTIO
    virtio_mmio_hvl_cb_run();
#endif
}

typedef void (*ipi_notify_t)(const struct device *dev);
__subsystem struct xlnx_ipi_api {
    ipi_notify_t notify;
};

static void xlnx_ipi_notify(const struct device *dev)
{
    WRITE32(dev, IPI_TRIG, (1 << 24));
}

static const struct xlnx_ipi_api xlnx_ipi_api = {
    .notify = xlnx_ipi_notify,
};

#define CREATE_XLNX_IPI_DEVICE(inst)                              \
    static void irq_config_##inst(const struct device *dev)          \
    {\
        IRQ_CONNECT(DT_INST_IRQ(inst, irq),\
                    DT_INST_IRQ(inst, priority),\
                    xlnx_ipi_isr,\
                    DEVICE_DT_INST_GET(inst),\
                    0);\
        irq_enable(DT_INST_IRQ(inst, irq)); \
    }\
    static struct xlnx_ipi_config xlnx_cfg_##inst = {      \
        DEVICE_MMIO_ROM_INIT(DT_DRV_INST(inst)),                     \
        irq_config_##inst,                                           \
    };                                                               \
    DEVICE_DT_INST_DEFINE(inst,                                      \
                          xlnx_ipi_init,                               \
                          NULL,                                      \
                          NULL,                                      \
                          &xlnx_cfg_##inst,                        \
                          PRE_KERNEL_1,               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,        \
                          &xlnx_ipi_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_XLNX_IPI_DEVICE)

void virtio_mmio_hvl_ipi(void *param) {
	const struct device *dev = NULL;

    (void)param;

	dev = DEVICE_DT_GET_ANY(xlnx_zynqmp_ipi);
	((struct xlnx_ipi_api *)dev->api)->notify(dev);
}

