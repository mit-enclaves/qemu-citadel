/*
 * QEMU RISC-V Zero Device 
 *
 * This provides Zero device emulation for QEMU.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/riscv/zero_device.h"
#include "qemu/error-report.h"

/* CPU read from a zero device address */
static uint64_t zero_device_mm_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

/* CPU wrote to a zero device address */
static void zero_device_mm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    qemu_log("A zero_device is read only: address %016" PRIx64 "\n",
            (uint64_t)addr);
    return;
}

static const MemoryRegionOps zero_device_mm_ops = {
    .read = zero_device_mm_read,
    .write = zero_device_mm_write,
};

ZeroDeviceState *zero_device_mm_init(MemoryRegion *address_space, MemoryRegion *main_mem, hwaddr base, hwaddr size)
{
    ZeroDeviceState *s = g_malloc0(sizeof(ZeroDeviceState));
    s->address_space = address_space;
    s->main_mem = main_mem;
    s->main_mem_ram_ptr = memory_region_get_ram_ptr(main_mem);
        memory_region_init_io(&s->mmio, NULL, &zero_device_mm_ops, s,
                              TYPE_ZERO_DEVICE, size);
        memory_region_add_subregion_overlap(address_space, base,
                                            &s->mmio, 1);

    return s;
}