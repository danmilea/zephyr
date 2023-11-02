.. _virtio-entropy-sample:

Sample virtio entropy test application
######################################

Overview
********

A simple sample that can be used to demonstrate virtio-based entropy generation.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/virtio/entropy
   :host-os: unix
   :board: qemu_cortex_a53
   :goals: run
   :compact:

Sample Output
=============

.. code-block:: console

    Running TESTSUITE entropy_api
    ===================================================================
    START - test_entropy_get_entropy
    random device is 0x4000b0d0, name is virt-rng
      0x02
      0x94
      0xec
      0x54
      0x82
      0xe9
      0xff
      0xaa
      0x98
     PASS - test_entropy_get_entropy in 0.001 seconds
    ===================================================================
    TESTSUITE entropy_api succeeded
    ===================================================================
    PROJECT EXECUTION SUCCESSFUL


Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.
