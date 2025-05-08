/*
 * Copyright (c) 2012-2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/init.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/drivers/entropy.h>

#define BUFFER_LENGTH 10
#define RECHECK_RANDOM_ENTROPY 0x10

extern char *net_sprint_addr(sa_family_t af, const void *addr);

#ifdef CONFIG_ENTROPY_GENERATOR
static int random_entropy(const struct device *dev, char *buffer, char num)
{
	int ret, i;
	int count = 0;

	(void)memset(buffer, num, BUFFER_LENGTH);

	/* The BUFFER_LENGTH-1 is used so the driver will not
	 * write the last byte of the buffer. If that last
	 * byte is not 0 on return it means the driver wrote
	 * outside the passed buffer, and that should never
	 * happen.
	 */
	ret = entropy_get_entropy(dev, buffer, BUFFER_LENGTH - 1);
	if (ret) {
		printk("Error: entropy_get_entropy failed: %d\n", ret);
		return -1;
	}

	if (buffer[BUFFER_LENGTH - 1] != num) {
		printk("Error: entropy_get_entropy buffer overflow\n");
		return -1;
	}

	for (i = 0; i < BUFFER_LENGTH - 1; i++) {
		printk("  0x%02x\n", buffer[i]);
		if (buffer[i] == num) {
			count++;
		}
	}

	if (count >= 2) {
		return RECHECK_RANDOM_ENTROPY;
	} else {
		return 0;
	}
}

/*
 * Function invokes the get_entropy callback in driver
 * to get the random data and fill to passed buffer
 */
static int get_entropy(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
	uint8_t buffer[BUFFER_LENGTH] = { 0 };
	int ret;

	if (!device_is_ready(dev)) {
		printk("error: random device not ready\n");
		return -1;
	}

	printk("random device is %p, name is %s\n",
		 dev, dev->name);

	ret = random_entropy(dev, buffer, 0);

	/* Check whether 20% or more of buffer still filled with default
	 * value(0), if yes then recheck again by filling nonzero value(0xa5)
	 * to buffer. Recheck random_entropy and verify whether 20% or more
	 * of buffer filled with value(0xa5) or not.
	 */
	if (ret == RECHECK_RANDOM_ENTROPY) {
		ret = random_entropy(dev, buffer, 0xa5);
		if (ret == RECHECK_RANDOM_ENTROPY) {
			return -1;
		} else {
			return ret;
		}
	}
	return ret;
}
#endif

extern uint32_t __shmem0_pool_start;
extern uint32_t __shmem0_end;

void virtio_mmio_hvl_wait(uint32_t usec)
{
	k_busy_wait(usec);
}

int main(void)
{
	printk("Board: %s\n", CONFIG_BOARD);

#ifdef CONFIG_ENTROPY_GENERATOR
	//rng
	printk("%s:%d - get_entropy test %s\n", __FUNCTION__, __LINE__,
		(get_entropy() != 0) ? "failed" : "passed");
#endif

#ifdef CONFIG_NETWORKING
	//net
	struct net_if *iface;

	iface = net_if_get_default();

	struct in_addr my_addr2 = { { { 192, 168, 200, 2 } } };
	struct in_addr my_mask2 = { { { 255, 255, 255, 0 } } };
	struct net_if_addr *ifaddr;
	ifaddr = net_if_ipv4_addr_add(iface, &my_addr2, NET_ADDR_MANUAL, 0);
	if (!ifaddr) {
		printk("Cannot add IPv4 address %s\n", net_sprint_addr(AF_INET, &my_addr2));
		return -1;
    }
	net_if_ipv4_set_netmask_by_addr(iface, &my_addr2, &my_mask2);

	//ifaddr->addr_state = NET_ADDR_PREFERRED;
	net_if_up(iface);
#endif

	return 0;
}
