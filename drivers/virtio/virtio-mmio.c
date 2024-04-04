/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/kernel.h>
#include <openamp/open_amp.h>
#include <openamp/virtqueue.h>
#include <openamp/virtio.h>
#include <metal/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/virtio/virtio.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/sys/device_mmio.h>

#define DT_DRV_COMPAT virtio_mmio

struct virtio_mmio_config {
    void (*irq_config)(const struct device *dev);
};

#define DEV_CFG(dev) ((struct virtio_mmio_config *)(dev->config))
#define DEV_DATA(dev) ((struct virtio_mmio_device *)(dev->data))

static int virtio_mmio_init(const struct device *dev);
struct virtio_device* virtio_mmio_get_virtio_device(const struct device *dev) {
    return &DEV_DATA(dev)->vdev;
}

static const struct virtio_driver_api virtio_mmio_api = {
    .get_virtio_device = virtio_mmio_get_virtio_device,
};

static struct k_heap hvl_shmem_k_heap;

void virtio_mmio_shm_pool_init(void *mem, size_t size)
{
    static int init = 0;
    if (!init) {
        k_heap_init(&hvl_shmem_k_heap, mem, size);
        init = 1;
    }
}

void* virtio_mmio_shm_pool_alloc(size_t size)
{
    return k_heap_alloc(&hvl_shmem_k_heap, size, K_NO_WAIT);
}

void virtio_mmio_shm_pool_free(void *ptr)
{
    k_heap_free(&hvl_shmem_k_heap, ptr);
}

/* LINKER_DT_RESERVED_MEM_GET_SIZE */
#define CREATE_VIRTIO_MMIO_DEVICE(inst)                              \
    static struct virtio_mmio_device virtio_mmio_data_##inst = {       \
        .vdev = { \
            .priv = &virtio_mmio_data_##inst, \
        }, \
        .irq = DT_INST_IRQN(inst),                               \
        .device_mode = DT_NODE_HAS_PROP(DT_INST(inst, DT_DRV_COMPAT), device_mode),   \
        .cfg_mem = { \
            .base = (uint8_t *)DT_INST_REG_ADDR(inst),                   \
            .size = (unsigned int)DT_INST_REG_SIZE(inst),              \
        }, \
        .shm_mem = { \
            .base = (uint8_t *)DT_REG_ADDR(DT_PHANDLE(DT_INST(inst, DT_DRV_COMPAT), memory_region)), \
            .size = (unsigned int)DT_REG_SIZE(DT_PHANDLE(DT_INST(inst, DT_DRV_COMPAT), memory_region)), \
        }, \
        .shm_device = {                                      \
            .name = DT_PROP(DT_INST(inst, DT_DRV_COMPAT), label),                         \
            .bus = NULL,                                     \
            .num_regions = 2,                                \
            {                                                \
                {                                            \
                    .virt       = (void *) NULL,         \
                    .physmap    = NULL,                  \
                    .size       = 0,                     \
                    .page_shift = (uintptr_t)0xffffffffffffffff,    \
                    .page_mask  = (uintptr_t)0xffffffffffffffff,    \
                    .mem_flags  = 0,                     \
                    .ops        = { NULL },              \
                },                                           \
                {                                            \
                    .virt       = (void *) NULL,         \
                    .physmap    = NULL,                  \
                    .size       = 0,                     \
                    .page_shift = (uintptr_t)0xffffffffffffffff,    \
                    .page_mask  = (uintptr_t)0xffffffffffffffff,    \
                    .mem_flags  = 0,                     \
                    .ops        = { NULL },              \
                },                                           \
            },                                               \
            .node = { NULL },                                \
            .irq_num = 0,                                    \
            .irq_info = NULL                                 \
        }                                                    \
    };                                                               \
    static void irq_config_##inst(const struct device *dev)          \
    {\
        IRQ_CONNECT(DT_INST_IRQ(inst, irq),\
                    DT_INST_IRQ(inst, priority),\
                    virtio_mmio_isr,\
                    &virtio_mmio_data_##inst.vdev,\
                    0);\
        irq_enable(DT_INST_IRQ(inst, irq));\
    }\
    static struct virtio_mmio_config virtio_mmio_cfg_##inst = {      \
        .irq_config = irq_config_##inst,                             \
    };                                                               \
    DEVICE_DT_INST_DEFINE(inst,                                      \
                          virtio_mmio_init,                          \
                          NULL,                                      \
                          &virtio_mmio_data_##inst,                  \
                          &virtio_mmio_cfg_##inst,                   \
                          PRE_KERNEL_1,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,        \
                          &virtio_mmio_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_MMIO_DEVICE)

#if defined(HVL_VIRTIO)
void virtio_mmio_hvl_ipi(void *param) __attribute__((weak));
void virtio_mmio_hvl_ipi(void *param) {}

struct hvl_dispatch hvl_func = {
	.hvl_shm_pool_init = virtio_mmio_shm_pool_init,
	.hvl_shm_pool_alloc = virtio_mmio_shm_pool_alloc,
	.hvl_shm_pool_free = virtio_mmio_shm_pool_free,
	.hvl_wait_cfg_event = virtio_mmio_hvl_wait_cfg_event,
	.hvl_ipi = virtio_mmio_hvl_ipi,
};
#endif

static int virtio_mmio_init(const struct device *dev)
{
    uintptr_t sram0_addr;
    uintptr_t virt_mem_ptr;
    uintptr_t cfg_mem_ptr;
    struct virtio_mmio_device *vmdev = DEV_DATA(dev);

#if defined(HVL_VIRTIO)
    vmdev->hvl_func = &hvl_func;
#endif

    /* Map config */
    cfg_mem_ptr = (uintptr_t)vmdev->cfg_mem.base;

#if defined(CONFIG_MMU)
    device_map(&cfg_mem_ptr, (uintptr_t)vmdev->cfg_mem.base,
               vmdev->cfg_mem.size, K_MEM_CACHE_NONE);
#endif

    //TODO: rework detection of full virtualization mode
    //For now, guest mode implies that the memory-region configured in the DTS
    //is the sram0 memory region, e.g. memory-region = <&sram0>;

    virt_mem_ptr = (uintptr_t)vmdev->shm_mem.base;
    sram0_addr = (uintptr_t)DT_REG_ADDR(DT_NODELABEL(sram0));

    if (sram0_addr == (uintptr_t)vmdev->shm_mem.base) {
        //memory already mapped
        virt_mem_ptr = (uintptr_t)Z_MEM_VIRT_ADDR((uintptr_t)vmdev->shm_mem.base);
    } else {
        /* Map dedicated mem region */
#if defined(CONFIG_MMU)
        device_map(&virt_mem_ptr, (uintptr_t)vmdev->shm_mem.base,
                    vmdev->shm_mem.size, K_MEM_CACHE_NONE);
#endif
    }

    if (virtio_mmio_device_init(vmdev, virt_mem_ptr, cfg_mem_ptr, (void *)dev))
        return -1;

    metal_log(METAL_LOG_DEBUG, "device %s @%p\n", dev->name, dev);
    metal_log(METAL_LOG_DEBUG, "iobase %lx\n", cfg_mem_ptr);

    DEV_CFG(dev)->irq_config(dev);

    return 0;
}

#if defined(HVL_VIRTIO)
void virtio_mmio_hvl_wait_cfg_event(struct virtio_device *vdev, uint32_t usec)
{
	struct virtio_mmio_device *vmdev = metal_container_of(vdev,
							      struct virtio_mmio_device, vdev);

    if (k_can_yield()) {
        if (vmdev && (vmdev->hvl_mode == 1)) {
            k_sem_take(&vmdev->cfg_sem, K_USEC(usec));
        }
    } else {
        k_busy_wait(usec);
    }
}
#endif
