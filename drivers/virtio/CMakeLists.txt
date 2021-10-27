#Copyright (c) 2021 Wind River Systems, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(virtio.c)
zephyr_library_sources_ifdef(CONFIG_VIRTIO_MMIO virtio-mmio.c)
zephyr_library_sources_ifdef(CONFIG_VIRTIO_SERIAL virtio-serial.c)
