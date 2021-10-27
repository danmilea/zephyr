/*
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <devicetree.h>
#include <device.h>
#include <sys/ring_buffer.h>
#include <drivers/virtio/virtio.h>
#include <drivers/uart.h>

#ifndef CONFIG_UART_INTERRUPT_DRIVEN
#define ALTERNATE_POLL /* simpler poll for no-interrupt case (WIP) */
#define SERIAL_PRIORITY    PRE_KERNEL_1
#else
#define SERIAL_PRIORITY    POST_KERNEL
#endif

#define printk(...) do {}while(0)

#define DT_DRV_COMPAT virtio_serial

#define DEV_CFG(dev) ((struct virtio_serial_config*)(dev->config))
#define DEV_DATA(dev) ((struct virtio_serial_data*)(dev->data))

#define VQIN_SIZE    4
#define VQOUT_SIZE   4
#define RXBUF_SIZE   8

struct virtio_serial_chan {
    const struct device *ser_dev;
    struct virtqueue *vqin, *vqout;
    struct ring_buf *rxfifo,*txfifo;
    atomic_t tx_inuse;
    atomic_t rx_inuse;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
        uart_irq_callback_user_data_t irq_cb;
        void *irq_cb_data;
        bool rx_irq_ena, tx_irq_ena;
        struct k_work cb_work;
#endif
    /* same number as queue size for vqin , size tbd*/
    char rxbufs[VQIN_SIZE][RXBUF_SIZE];
    int rxbuf;
    bool rxpoll_active;
    uint8_t rxpoll;
};

struct virtio_serial_config {
    const struct device *bus;
    int vq_count;
    struct virtqueue **vqs;
};

struct virtio_serial_data {
    struct virtio_serial_chan *chan0;
};

static int virtio_serial_poll_in(const struct device *dev, unsigned char *p_char);
static void virtio_serial_poll_out(const struct device *dev, unsigned char out_char);
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
static void virtio_serial_irq_callback_set(const struct device *dev,
                                 uart_irq_callback_user_data_t cb,
                                 void *user_data);
static int virtio_serial_irq_update(const struct device *dev);
static int virtio_serial_irq_tx_ready(const struct device *dev);
static void virtio_serial_irq_tx_disable(const struct device *dev);
static void virtio_serial_irq_tx_enable(const struct device *dev);
static int virtio_serial_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size);

static int virtio_serial_irq_rx_ready(const struct device *dev);
static void virtio_serial_irq_rx_disable(const struct device *dev);
static void virtio_serial_irq_rx_enable(const struct device *dev);
static int virtio_serial_fifo_read(const struct device *dev, uint8_t *rx_data, const int size);

static void virtio_serial_irq_callback_work_handler(struct k_work *work);
#endif /* defined(CONFIG_UART_INTERRUPT_DRIVEN) */

#if !defined(ALTERNATE_POLL)
static void virtio_serial_vqin_cb(void*);
static void virtio_serial_vqout_cb(void*);
static void virtio_serial_rx_refill(const struct device *dev);
#endif

struct uart_driver_api virtio_serial_api = {
    .poll_in = virtio_serial_poll_in,
    .poll_out = virtio_serial_poll_out,
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
    .irq_callback_set = virtio_serial_irq_callback_set,
    .irq_update = virtio_serial_irq_update,
    .irq_tx_ready = virtio_serial_irq_tx_ready,
    .irq_tx_disable = virtio_serial_irq_tx_disable,
    .irq_tx_enable = virtio_serial_irq_tx_enable,
    .fifo_fill = virtio_serial_fifo_fill,
    .irq_rx_ready = virtio_serial_irq_rx_ready,
    .irq_rx_enable = virtio_serial_irq_rx_disable,
    .irq_rx_enable = virtio_serial_irq_rx_enable,
    .fifo_read = virtio_serial_fifo_read,

#endif /* defined(CONFIG_UART_INTERRUPT_DRIVEN) */
};

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
K_KERNEL_STACK_DEFINE(virtio_serial_stack, 2048);
static struct k_work_q virtio_serial_workq;
static bool workq_on = false;
#endif


#define CREATE_VIRTIO_SERIAL_DEVICE(inst) \
    VQ_DECLARE(vq0_##inst, VQIN_SIZE, 4096);\
    VQ_DECLARE(vq1_##inst, VQOUT_SIZE, 4096);\
    struct virtqueue *vq_list_##inst[] = {VQ_PTR(vq0_##inst), VQ_PTR(vq1_##inst)};\
    RING_BUF_DECLARE(__ringbuf_rx_##inst, 32);\
    RING_BUF_DECLARE(__ringbuf_tx_##inst, 32);\
    static struct virtio_serial_chan __chan0__##inst = {\
        .tx_inuse = ATOMIC_INIT(0),\
        .rx_inuse = ATOMIC_INIT(0),\
        .rxfifo = &__ringbuf_rx_##inst,\
        .txfifo = &__ringbuf_tx_##inst,\
        .rxpoll_active = false,\
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
    virtio_serial_init,\
    NULL,\
    &virtio_serial_data_##inst,\
    &virtio_serial_cfg_##inst,\
    SERIAL_PRIORITY,\
    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,\
    &virtio_serial_api);

static int virtio_serial_init(const struct device *dev)
{
    uint32_t devid, features;

    printk("%s()\n",__FUNCTION__);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
    if (!workq_on)
        {
        k_work_queue_start(&virtio_serial_workq, virtio_serial_stack,
                           K_KERNEL_STACK_SIZEOF(virtio_serial_stack),
                           K_PRIO_COOP(1), NULL);
        k_thread_name_set(&virtio_serial_workq.thread, "virtio_serial_workq");
        workq_on = true;
        }
    k_work_init(&DEV_DATA(dev)->chan0->cb_work, virtio_serial_irq_callback_work_handler);
#endif

    __ASSERT(DEV_CFG(dev)->bus != NULL, "DEV_CFG(dev)->bus != NULL");
    if (!device_is_ready(DEV_CFG(dev)->bus))
        return -1;
    printk("bus %p\n", DEV_CFG(dev)->bus);
    devid = virtio_get_devid(DEV_CFG(dev)->bus);
    if (devid != VIRTIO_ID_CONSOLE)
        {
        printk("Bad devid %08x\n", devid);
        return -1;
        }
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER);
    virtio_set_features(DEV_CFG(dev)->bus, 0/*VIRTIO_F_NOTIFY_ON_EMPTY*/);
    features =virtio_get_features(DEV_CFG(dev)->bus);
    printk("features: %08x\n", features);

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

    DEV_DATA(dev)->chan0->ser_dev = dev;
    DEV_DATA(dev)->chan0->rxbuf = 0;

    virtio_register_device(DEV_CFG(dev)->bus, DEV_CFG(dev)->vq_count, DEV_CFG(dev)->vqs);
    virtio_set_status(DEV_CFG(dev)->bus, VIRTIO_CONFIG_STATUS_DRIVER_OK);
    virtqueue_notify(DEV_DATA(dev)->chan0->vqin);
    virtqueue_notify(DEV_DATA(dev)->chan0->vqout);

    return 0;
}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
static void virtio_serial_irq_callback_work_handler(struct k_work *work)
{
        struct virtio_serial_chan *chan;

        chan = CONTAINER_OF(work, struct virtio_serial_chan, cb_work);

        chan->irq_cb(chan->ser_dev, chan->irq_cb_data);
}
#endif

static int virtio_serial_poll_in(const struct device *dev, unsigned char *p_char)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
#if !defined(ALTERNATE_POLL)
    uint32_t length;
    uint8_t data;

    if (k_is_in_isr()) {
        /* I don't think this is recoverable */
        if (atomic_cas(&chan->rx_inuse, 0, 1) == false)
            return -1;
        /* caller better not retry this, nothing we can do in ISR context */
    }
    else {
        while (
            (atomic_cas(&chan->rx_inuse, 0, 1) == false)
            ) {
#if defined(CONFIG_MULTITHREADING)
            /* k_sleep allows other threads to execute and finish
             * their transactions.
             */
            k_msleep(1);
#else
            k_busy_wait(1000);
#endif
        }
    }

    /* we have exclusive access to the ring */
    /* Check if any descriptor was used */
    if (k_is_in_isr())
            virtio_serial_vqin_cb(chan);
    /* Check for data */
    length = ring_buf_get(chan->rxfifo, &data, 1);
    if (length == 1) {
        *p_char = data;
        atomic_set(&chan->rx_inuse, 0);
        return 0;
    }
    /* No data, put a descriptor on the VQ */
    virtio_serial_rx_refill(chan->ser_dev);

    atomic_set(&chan->rx_inuse, 0);
    return -1; /* caller can retry */
#else
    int ret = -1;
    if (atomic_cas(&chan->rx_inuse, 0, 1) == false)
        return ret;
    if (chan->rxpoll_active) {
        void *cookie = virtqueue_dequeue_buf(chan->vqin, NULL);
        if (!cookie) {
            /* Nothing received yet */
            atomic_set(&chan->rx_inuse, 0);
            return -1;
        }
        *p_char = chan->rxpoll;
        ret = 0;
    }
    virtqueue_enqueue_buf(chan->vqin, (void*)(uintptr_t)1, 1, &chan->rxpoll, 1);
    virtqueue_notify(chan->vqin);
    chan->rxpoll_active = true;
    atomic_set(&chan->rx_inuse, 0);
    return ret;
#endif
}


static void virtio_serial_poll_out(const struct device *dev, unsigned char out_char)
{
//printk here is a bad idea when the console is on virtio_serial
//    printk("%s(%c)\n", __FUNCTION__, out_char);
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    uint32_t length;
    uint8_t *data;

#if !defined(ALTERNATE_POLL)
    if (k_is_in_isr()) {
        /* I don't think this is recoverable */
        if (atomic_cas(&chan->tx_inuse, 0, 1) == false)
            return;
        /* try to dequeue until we get a descriptor or buffer space */
        while ( virtqueue_full(chan->vqout) ||
                (ring_buf_space_get(chan->txfifo) == 0)) {
            virtio_serial_vqout_cb(chan);
            }
    }
    else {
        /*
         * atomic_cas() is evaluated only if virtqueue_full() and
         * ring_buf_space_get() conditions are false
         */
        while (
            virtqueue_full(chan->vqout) ||
            (ring_buf_space_get(chan->txfifo) == 0) ||
            (atomic_cas(&chan->tx_inuse, 0, 1) == false)
            ) {
#if defined(CONFIG_MULTITHREADING)
            /* k_sleep allows other threads to execute and finish
             * their transactions.
             */
            k_msleep(1);
#else
            k_busy_wait(1000);
#endif
        }
    }
    /* we have exclusive access to the ring */
    length = ring_buf_put_claim(chan->txfifo, &data, 1);
    __ASSERT(length, "no space in the ring buffer");
    data[0] = out_char;
    ring_buf_put_finish(chan->txfifo, 1);
    virtqueue_enqueue_buf(chan->vqout, (void*)(uintptr_t)1, 0, data, 1);
    virtqueue_notify(chan->vqout);
    atomic_set(&chan->tx_inuse, 0);
#else
    if (atomic_cas(&chan->tx_inuse, 0, 1) == false)
        return;

    length = ring_buf_put_claim(chan->txfifo, &data, 1);

    if ((length == 0) || virtqueue_full(chan->vqout)) {
        void *cookie = virtqueue_dequeue_buf(chan->vqout, NULL);
        if (cookie)
            ring_buf_get(chan->txfifo, NULL, (uint32_t)(uintptr_t)cookie);
        }
    if (!length)
        length = ring_buf_put_claim(chan->txfifo, &data, 1);
    if ((length == 0) || virtqueue_full(chan->vqout)) {
        /* We tried, at least */
        atomic_set(&chan->tx_inuse, 0);
        return;
    }
    data[0] = out_char;
    ring_buf_put_finish(chan->txfifo, 1);
    virtqueue_enqueue_buf(chan->vqout, (void*)(uintptr_t)1, 0, data, 1);
    virtqueue_notify(chan->vqout);
    atomic_set(&chan->tx_inuse, 0);
#endif
}

#if !defined(ALTERNATE_POLL)
static void virtio_serial_vqin_cb(void *arg)
{
    struct virtio_serial_chan *chan = arg;
    void *cookie;
    int length;
    uint32_t written;
    bool rx_ready = false;
    printk("vqin\n");

    while((cookie = virtqueue_dequeue_buf(chan->vqin, &length))) {
        printk("got %d\n", length);

        written = ring_buf_put(chan->rxfifo, cookie, length);
        if (written < length)
            printk("%d discarded\n", length - written);
        if (written > 0)
            rx_ready = true;
        virtio_serial_rx_refill(chan->ser_dev);
    };
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
    if (rx_ready && chan->rx_irq_ena && chan->irq_cb)
        k_work_submit_to_queue(&virtio_serial_workq, &chan->cb_work);

#endif
}

static void virtio_serial_vqout_cb(void *arg)
{
    struct virtio_serial_chan *chan = arg;
    void *cookie;
    int length;
    bool tx_ready = false;
    printk("vqout\n");
    do {
        cookie = virtqueue_dequeue_buf(chan->vqout, NULL);
        if (cookie) {
            length = (int)(uintptr_t)cookie;
            ring_buf_get(chan->txfifo, NULL, length);
            tx_ready = true;
            }
    } while(cookie);
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
// tx interrupt is suposed to happen when fifo is empty or when space is available?
//    if (tx_ready && chan->tx_irq_ena && chan->irq_cb)
    if (virtqueue_empty(chan->vqout) && chan->tx_irq_ena && chan->irq_cb)
        k_work_submit_to_queue(&virtio_serial_workq, &chan->cb_work);
#endif
}
#endif

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
static void virtio_serial_irq_callback_set(const struct device *dev,
                                 uart_irq_callback_user_data_t cb,
                                 void *user_data)
{
    printk("%s( %p , %p )\n", __FUNCTION__, cb, user_data);
    DEV_DATA(dev)->chan0->irq_cb = cb;
    DEV_DATA(dev)->chan0->irq_cb_data = user_data;
}

static int virtio_serial_irq_update(const struct device *dev)
{
    printk("%s( %p )\n", __FUNCTION__, dev);
    return 1;
}

static int virtio_serial_irq_tx_ready(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    int space;
    printk("%s( %p )\n", __FUNCTION__, dev);
    if (virtqueue_full(chan->vqout))
        return 0;
    space = ring_buf_space_get(chan->txfifo);
    return !!space;
}

static void virtio_serial_irq_tx_disable(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    chan->tx_irq_ena = false;
}

static void virtio_serial_irq_tx_enable(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    chan->tx_irq_ena = true;
    if (chan->irq_cb && !virtqueue_full(chan->vqout) && ring_buf_space_get(chan->txfifo))
        k_work_submit_to_queue(&virtio_serial_workq, &chan->cb_work);
}

static int virtio_serial_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    uint32_t length;
    uint8_t *data;
    printk("%s( %p )\n", __FUNCTION__, dev);
    if (atomic_cas(&chan->tx_inuse, 0, 1) == false)
        return 0;
    /* caller is supposed to use uart_irq_tx_ready() first */
    __ASSERT(!virtqueue_full(chan->vqout), "no descriptors");
    length = ring_buf_put_claim(chan->txfifo, &data, size);
    __ASSERT(length, "no space in the ring buffer");
    memcpy(data, tx_data, length);
    ring_buf_put_finish(chan->txfifo, length);
    virtqueue_enqueue_buf(chan->vqout, (void*)(uintptr_t)length, 0, data, length);
    virtqueue_notify(chan->vqout);
    atomic_set(&chan->tx_inuse, 0);
    return length;
}

static int virtio_serial_irq_rx_ready(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    uint8_t tmp;
    printk("%s( %p )\n", __FUNCTION__, dev);
    return !!ring_buf_peek(chan->rxfifo, &tmp, 1);
}

static void virtio_serial_irq_rx_disable(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    chan->rx_irq_ena = false;
}

static void virtio_serial_irq_rx_enable(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    printk("%s( %p )\n", __FUNCTION__, dev);
    chan->rx_irq_ena = true;
    virtio_serial_rx_refill(dev);
}

static int virtio_serial_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    uint32_t length;
    printk("%s( %p %p %d)\n", __FUNCTION__, dev, rx_data, size);
    if (atomic_cas(&chan->rx_inuse, 0, 1) == false)
        return 0;
    length = ring_buf_get(chan->rxfifo, rx_data, size);
    atomic_set(&chan->rx_inuse, 0);
    return length;
}

#endif /* defined(CONFIG_UART_INTERRUPT_DRIVEN) */

#if !defined(ALTERNATE_POLL)
static void virtio_serial_rx_refill(const struct device *dev)
{
    struct virtio_serial_chan *chan = DEV_DATA(dev)->chan0;
    uint8_t *data;

    printk("%s( %p )\n", __FUNCTION__, dev);
    /* review this and maybe turn into __ASSERT() */
    if (virtqueue_full(chan->vqin))
        return;
    data = chan->rxbufs[chan->rxbuf++];
    chan->rxbuf %= VQIN_SIZE;
    virtqueue_enqueue_buf(chan->vqin, data, 1, data, RXBUF_SIZE);
    virtqueue_notify(chan->vqin);
}
#endif

DT_INST_FOREACH_STATUS_OKAY(CREATE_VIRTIO_SERIAL_DEVICE)
