.. _virtio-entropy-net-hypervisorless-sample:


###########

Overview
********

A simple sample that can be used to demonstrate hypervisorless virtio with
entropy and network devices.

Building
********

Set

.. code-block:: console

    option (WITH_HVL_VIRTIO "Build with hypervisor-less virtio (front end) enabled" ON)

in modules/lib/open-amp/cmake/options.cmake

The application can then be built as follows:

.. code-block:: console

    west build -p auto -b qemu_cortex_r5 zephyr/samples/virtio/hvl_net_rng_reloc

Refer to the following document for information on hypervisorless virtio operation and
build environment.

https://github.com/danmilea/hypervisorless_virtio_zcu102/blob/main/README.md

