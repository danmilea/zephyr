/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <kernel.h>
#include <sys/printk.h>
#include <init.h>
#include <net/net_if.h>
#include <net/net_core.h>

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
