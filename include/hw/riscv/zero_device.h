/*
 * ZERO_DEVICE
 */

#ifndef HW_ZERO_DEVICE_H
#define HW_ZERO_DEVICE_H


#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "exec/memory.h"
#include "target/riscv/cpu.h"

#define TYPE_ZERO_DEVICE "riscv.zero_device"

#define ZERO_DEVICE(obj) \
    OBJECT_CHECK(ZeroDeviceState, (obj), TYPE_ZERO_DEVICE)

typedef struct ZeroDeviceState {
    /*< private >*/
    //SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    MemoryRegion *address_space;
    MemoryRegion *main_mem;
    void *main_mem_ram_ptr;
} ZeroDeviceState;

ZeroDeviceState *zero_device_mm_init(MemoryRegion *address_space, MemoryRegion *main_mem, hwaddr addr, hwaddr size);

#endif