/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <devicetree.h>
#include <device.h>
#include <stdio.h>
#include <string.h>
#include <sys/mutex.h>

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

void ipi();

#ifdef RSLD
void wait_hvl_config(uint32_t *ptr, uint32_t data) {
    static int i = 0;
    i++;
	if (*ptr == data)
		{
		return;
		}

    k_busy_wait(1 * USEC_PER_SEC);
	wait_hvl_config(ptr, data);
}
#endif

struct xlnx_ipi_config {
    DEVICE_MMIO_ROM;
    void (*irq_config)(const struct device *dev);
};

static int xlnx_ipi_init(const struct device *dev);
static void xlnx_ipi_isr(const struct device *dev);

#ifdef RSLD
/* callback framework */
#define MAX_CB_FUNCS 10

typedef void (*hvl_funcptr)(void *);

static hvl_funcptr _cb_funcs[MAX_CB_FUNCS];
static void *_cb_args[MAX_CB_FUNCS];
static int _num_cb_funcs;

int rslHvlCbSet(hvl_funcptr func, void *arg)
{
	if (_num_cb_funcs < MAX_CB_FUNCS - 1)
	{
		_cb_funcs[_num_cb_funcs] = func;
		_cb_args[_num_cb_funcs] = arg;
		_num_cb_funcs++;
		return 0;
	}
	return -1;
}
#endif

static int xlnx_ipi_init(const struct device *dev)
{
    //printk("z: %s:%d\n", __FUNCTION__, __LINE__);

    DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

    WRITE32(dev, IPI_IDR, 0xFFFFFFFF);
    WRITE32(dev, IPI_ISR, 0xFFFFFFFF);
    ((struct xlnx_ipi_config *)dev->config)->irq_config(dev);
    //WRITE32(dev, IPI_IER, 1 << 24);
    WRITE32(dev, IPI_IER, (1 << 24) | (1 << 8));
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

#ifdef RSLD
    /* run callbacks */
    for (i = 0; i < _num_cb_funcs; i++) {
		if (_cb_funcs[i] != NULL)
		{
			_cb_funcs[i](_cb_args[i]);
		}
	}
#endif
}

typedef void (*ipi_notify_t)(const struct device *dev);
__subsystem struct xlnx_ipi_api {
    ipi_notify_t notify;
};

static void xlnx_ipi_notify(const struct device *dev)
{
    //printk("-- %s dev: %p\n", __func__, dev);
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
                          /*POST_KERNEL*/PRE_KERNEL_1,               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,        \
                          &xlnx_ipi_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_XLNX_IPI_DEVICE)

void ipi() {
	const struct device *dev = NULL;
	dev = DEVICE_DT_GET_ANY(xlnx_zynqmp_ipi);
	//printk("%s:%d - dev = %p\n", __FUNCTION__, __LINE__, dev);
	((struct xlnx_ipi_api *)dev->api)->notify(dev);
}

