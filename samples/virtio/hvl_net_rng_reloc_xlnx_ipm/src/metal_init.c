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
#include <metal/device.h>

int init_metal()
{
	int32_t err;
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	metal_params.log_level = METAL_LOG_DEBUG;

	err = metal_init(&metal_params);
	if (err) {
		metal_log(METAL_LOG_ERROR, "metal_init: failed - error code %d\n", err);
		return -ENODEV;
	}

	return 0;
}
SYS_INIT(init_metal, PRE_KERNEL_1, 49);
