.. _virtio-net-dhcpv4-client-sample:

Sample DHCPv4 client application using virtio-net
#################################################

Overview
********

This application starts a DHCPv4 client and gets an IPv4 address from the
DHCPv4 server.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/virtio/net/dhcp
   :host-os: unix
   :board: qemu_cortex_a53
   :goals: run
   :compact:

You can that Zephyr application client is running and has received
an ip address by typing:

.. code-block:: console

    uart:~$ net iface

    Interface 0x400aa018 (Ethernet) [1]
    ===================================
    Link addr : 52:54:00:12:34:56
    MTU       : 1500
    Flags     : AUTO_START,IPv4
    Ethernet capabilities supported:
    IPv4 unicast addresses (max 1):
    	10.0.2.15 DHCP preferred
    IPv4 multicast addresses (max 1):
    	<none>
    IPv4 gateway : 10.0.2.2
    IPv4 netmask : 255.255.255.0
    DHCPv4 lease time : 86400
    DHCPv4 renew time : 43200
    DHCPv4 server     : 10.0.2.2
    DHCPv4 requested  : 10.0.2.15
    DHCPv4 state      : bound
    DHCPv4 attempts   : 1

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.
