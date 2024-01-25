/*
 * QEMU RISC-V Sanctum Emulator
 *
 * Copyright (c) 2019 Ilia Lebedev, MIT
 *
 * This provides a RISC-V Board with the following devices:
 *
 * 0) HTIF Console and Poweroff
 * 1) CLINT (Timer and IPI)
 * 2) PLIC (Platform Level Interrupt Controller)
 * 3) PUF Model (Platform Level Interrupt Controller)
 * 4) ELF Loader (Platform Level Interrupt Controller)
 * 5) Boot ROM initialized from "firmware" file, given as argument
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/puf.h"
#include "hw/riscv/zero_device.h"
#include "hw/char/riscv_htif.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/riscv/sanctum.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "elf.h"

#include <libfdt.h>

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} sanctum_memmap[] = {
    [SANCTUM_MROM] =        {      0x1000,    0x11000 },
    [SANCTUM_PUF] =         {    0x200000,       0x20 },
    [SANCTUM_ELFLD] =       {   0x1000000,     0x1000 },
    [SANCTUM_CLINT] =       {   0x2000000,    0xc0000 },
    [SANCTUM_DRAM] =        {  0x80000000, 0x80000000 },
    [SANCTUM_ZERO_DEVICE] = { 0x180000000, 0x80000000 },
    [SANCTUM_LLC_CTRL] =    { 0x200000000,        0x8 },
};

static uint64_t load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf_ram_sym(kernel_filename, NULL, NULL, NULL,
            &kernel_entry, NULL, &kernel_high, NULL, 0, EM_RISCV, 1, 0,
            NULL, true, htif_symbol_callback) < 0) {
        error_report("could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    return kernel_entry;
}

static void create_fdt(SanctumState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t phandle = 1;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "ucbbar,sanctum-bare,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "ucbbar,spike-bare-dev");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/htif");
    qemu_fdt_setprop_string(fdt, "/htif", "compatible", "ucb,htif0");

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[SANCTUM_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[SANCTUM_DRAM].base >> 32, memmap[SANCTUM_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    phandle += s->soc.num_harts;
    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        
        int cpu_phandle = phandle - (cpu + 1);
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
                              SANCTUM_CLOCK_FREQ);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", cpu_phandle);
        qemu_fdt_setprop_cell(fdt, intc, "linux,phandle", cpu_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/clint@%lx",
        (long)memmap[SANCTUM_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SANCTUM_CLINT].base,
        0x0, memmap[SANCTUM_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    if (cmdline) {
        qemu_fdt_add_subnode(fdt, "/chosen");
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
 }

static void sanctum_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = sanctum_memmap;

    SanctumState *s = g_new0(SanctumState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    MemoryRegion *elfld_rom = g_new(MemoryRegion, 1);
    MemoryRegion *llc_controller = g_new(MemoryRegion, 1);
    int i;

    int base_hartid = 0;
    int hart_count = machine->smp.cpus;
    bool htif_custom_base = false;

    /* Ensure the requested configuration is legal for Sanctum */
    assert(TARGET_RISCV64);
    assert(PGSHIFT == 12);
    assert (machine->ram_size == 0x80000000); // Due to hacks on hacks on hack emulator is only defined for a machine with 2GB DRAM and 64 "regions" for enclave isolation.

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", TYPE_SANCTUM_MACHINE,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "hartid-base", base_hartid,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "num-harts", hart_count,
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), "realized", true,
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.sanctum.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SANCTUM_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.sanctum.mrom",
                           memmap[SANCTUM_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SANCTUM_MROM].base,
                                mask_rom);

    /* zero device */
    zero_device_mm_init(system_memory, mask_rom, memmap[SANCTUM_ZERO_DEVICE].base, memmap[SANCTUM_ZERO_DEVICE].size);

    /* LLC Partition Controller */
    memory_region_init_ram(llc_controller, NULL, "riscv.sanctum.llc_controler",
                           memmap[SANCTUM_LLC_CTRL].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SANCTUM_LLC_CTRL].base,
                                llc_controller); 

    if (machine->kernel_filename) {
        load_kernel(machine->kernel_filename);
    }

    /* reset vector */
    char *reset_vec;
    uint32_t reset_vec_size = 0;

    uint32_t default_reset_vec[4] = {
        0x000402b7,                  // 0: lui	t0,0x40
        0x0012829b,                  // 4: addiw	t0,t0,1
        0x00d29293,                  // 8: slli	t0,t0,0xd
        0x00028067,                  // C: jr	t0 # Jump to 0x80002000
    };

    /* the reset vector is in little_endian byte order */
    for (i = 0; i < sizeof(default_reset_vec) >> 2; i++) {
        default_reset_vec[i] = cpu_to_le32(default_reset_vec[i]);
    }

    /* Load custom bootloader, if requested, else use default above */
    if (machine->firmware) {
        FILE *bootloader_file;

        bootloader_file = fopen ( machine->firmware , "rb" );
        if( !bootloader_file ) perror("Failed to open the bootloader file."),exit(1);

        fseek( bootloader_file , 0L , SEEK_END);
        reset_vec_size = ftell( bootloader_file );
        rewind( bootloader_file );

        reset_vec = (char*)malloc(reset_vec_size * sizeof(char));
        if( !reset_vec ) fclose(bootloader_file),fputs("Failed to allocate space to read the bootloader file.",stderr),exit(1);

        if( 1!=fread( reset_vec , reset_vec_size, 1 , bootloader_file) )
            fclose(bootloader_file),free(reset_vec),fputs("Failed to read entire bootloader file.",stderr),exit(1);

        fclose(bootloader_file);
    } else {
        reset_vec = (char*)default_reset_vec;
        reset_vec_size = sizeof(default_reset_vec);
    }

    /* copy in the reset vector */
    rom_add_blob_fixed_as("mrom.reset", reset_vec, reset_vec_size,
                               memmap[SANCTUM_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[SANCTUM_MROM].size - reset_vec_size) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[SANCTUM_MROM].base + reset_vec_size,
                          &address_space_memory);

    /* PUF */
    puf_create(memmap[SANCTUM_PUF].base, memmap[SANCTUM_PUF].size,
               0xDEADBEEFABADCAFEL);

    /* ELF loader module */
    memory_region_init_rom(elfld_rom, NULL, "riscv.sanctum.elfldr",
                           memmap[SANCTUM_ELFLD].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SANCTUM_ELFLD].base,
                                elfld_rom);
    uint64_t oxo[2] = { 0x0L, 0x0L };
    rom_add_blob_fixed_as("elfldr.status", oxo, 0x10,
                          memmap[SANCTUM_ELFLD].base,
                          &address_space_memory);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, serial_hd(0), memmap[SANCTUM_ELFLD].base, htif_custom_base);

    /* Core Local Interruptor (timer and IPI) */
    riscv_aclint_swi_create(memmap[SANCTUM_CLINT].base, base_hartid, hart_count, false);
    riscv_aclint_mtimer_create(
        memmap[SANCTUM_CLINT].base + RISCV_ACLINT_SWI_SIZE,
        RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, false);

}

static void sanctum_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Sanctum Board";
    mc->init = sanctum_board_init;
    mc->max_cpus = 4;
    mc->is_default = 1;
}

DEFINE_MACHINE("sanctum", sanctum_machine_init)
