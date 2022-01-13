/*
 * Copyright (c) Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <autoconf.h>
#include <linker/sections.h>
#include <devicetree.h>

#include <linker/linker-defs.h>
#include <linker/linker-tool.h>

#define CONFIG_SRAM2 1
#define _SRAM2_DATA_SECTION_NAME  .sram2_data
#define _SRAM2_BSS_SECTION_NAME   .sram2_bss
#define _SRAM2_TEXT_SECTION_NAME  .sram2_text
#define SRAM2_ADDR 0x78100000
#define SRAM2_SIZE (16384 * 1K)

#define RAMABLE_REGION SRAM2
//#define ROMABLE_REGION SRAM2

//Needs entry in dts to allow remoteproc to access memory at 0x77000000
//E.g. xilinx-zcu102-2020.2/components/plnx_workspace/device-tree/device-tree/system-user.dtsi
#define SHMEM_START_ADDR 0x77000000

#define SHMEM_RESERVED_HEADERS 4K
#define SHMEM_RESERVED_SIZE (16384 * 1K)

MEMORY
{
    LINKER_DT_RESERVED_MEM_REGIONS()
    shmem0 (rw) : ORIGIN = (SHMEM_START_ADDR + SHMEM_RESERVED_HEADERS), LENGTH = (SHMEM_RESERVED_SIZE - SHMEM_RESERVED_HEADERS)
#ifdef CONFIG_SRAM2
    SRAM2                  (wx) : ORIGIN = (SRAM2_ADDR), LENGTH = SRAM2_SIZE
#endif
}

SECTIONS
{
    LINKER_DT_RESERVED_MEM_SECTIONS()
    shmem0 (NOLOAD): { __shmem0_start = .; KEEP(*(.shared.vring.*)) __shmem0_pool_start = .; __shmem0_end = __shmem0_start + (SHMEM_RESERVED_SIZE - SHMEM_RESERVED_HEADERS); } > shmem0
}

#include <arch/arm/aarch32/cortex_r/scripts/linker.ld>
