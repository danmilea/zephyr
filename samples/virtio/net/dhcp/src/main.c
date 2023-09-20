/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/init.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <metal/device.h>

void main(void)
{
	printk("Hello World! %s\n", CONFIG_BOARD);
        struct net_if *iface;

#ifdef CONFIG_NETWORKING
        iface = net_if_get_default();
#endif

#ifdef CONFIG_NET_DHCPV4
        net_dhcpv4_start(iface);
#endif
}

#define PRINTK_TEST(level) \
int _test_##level()\
{\
	printk("%s()\n",__FUNCTION__);\
	return 0;\
}\
SYS_INIT(_test_##level,level, 99);

int init_metal(const struct device *dev)
{
	int32_t err;
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	metal_params.log_level = METAL_LOG_DEBUG;

	(void)dev;

	err = metal_init(&metal_params);
	if (err) {
		metal_log(METAL_LOG_ERROR, "metal_init: failed - error code %d\n", err);
		return -ENODEV;
	}

	return 0;
}
SYS_INIT(init_metal, PRE_KERNEL_1, 49);

PRINTK_TEST(PRE_KERNEL_1)
PRINTK_TEST(PRE_KERNEL_2)
PRINTK_TEST(POST_KERNEL)
PRINTK_TEST(APPLICATION)

void printk_timer_expired(struct k_timer *timer)
{
	printk("%s()\n",__FUNCTION__);
}

K_TIMER_DEFINE(printk_test_timer, printk_timer_expired, NULL);

int printk_timer_start()
{
	k_timer_start(& printk_test_timer, K_MSEC(10), K_SECONDS(10));
	return 0;
}

//SYS_INIT(printk_timer_start,PRE_KERNEL_1, 99);
