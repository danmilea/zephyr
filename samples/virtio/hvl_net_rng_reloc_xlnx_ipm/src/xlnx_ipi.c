/*
 * Copyright (c) 2021-2024 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/drivers/ipm.h>
#include <zephyr/logging/log.h>
#include <openamp/virtio_mmio.h>
#ifdef HVL_VIRTIO
#include <openamp/virtio_mmio_hvl.h>
#endif

LOG_MODULE_REGISTER(xlnx_ipi, LOG_LEVEL_DBG);

static const struct device *const ipm_handle =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

static void platform_ipm_callback(const struct device *dev, void *context,
				  uint32_t id, volatile void *data)
{
#ifdef HVL_VIRTIO
    virtio_mmio_hvl_cb_run();
#endif
}

int platform_init() {
	int status;

	if (!device_is_ready(ipm_handle)) {
		LOG_ERR("%s:%d - IPM device is not ready\n", __FUNCTION__, __LINE__);
		return -1;
	}

	ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);
	status = ipm_set_enabled(ipm_handle, 1);
	if (status) {
		LOG_ERR("%s:%d - ipm_set_enabled failed\n", __FUNCTION__, __LINE__);
		return -1;
	}

	return 0;
}

void virtio_mmio_hvl_ipi(void *param) {

    (void)param;

	ipm_send(ipm_handle, 0, 7, NULL, 0);
}


int init_ipm() {
	int ret = platform_init();
	if (ret) {
		LOG_ERR("%s:%d - failed to initialize platform\n", __FUNCTION__, __LINE__);
		ret = -1;
	} else {
		LOG_DBG("%s:%d - platform initialized\n", __FUNCTION__, __LINE__);
	}

    return ret;
}

SYS_INIT(init_ipm, PRE_KERNEL_1, 49);
