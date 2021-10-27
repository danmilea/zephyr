# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

list(APPEND QEMU_EXTRA_FLAGS -device virtio-serial-device)
list(APPEND QEMU_EXTRA_FLAGS -chardev socket,path=/tmp/foo,server=on,wait=off,id=foo)
list(APPEND QEMU_EXTRA_FLAGS -device virtconsole,chardev=foo)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.c)

