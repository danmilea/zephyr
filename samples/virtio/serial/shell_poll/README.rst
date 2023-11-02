.. _virtio-shell-poll-sample:

Sample virtio shell application
###############################

Overview
********

A simple sample that can be used to demonstrate a virtio-based serial console.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/virtio/serial/shell_poll
   :host-os: unix
   :board: qemu_cortex_a53
   :goals: run
   :compact:

For a shell session use:

.. code-block:: console

    socat - /tmp/foo

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.
