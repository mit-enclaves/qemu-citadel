/*
 * Sanctum machine interface
 *
 * Copyright (c) 2019 Ilia Lebedev, MIT.
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

#ifndef HW_RISCV_SANCTUM_H
#define HW_RISCV_SANCTUM_H

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState soc;
    void *fdt;
    int fdt_size;
} SanctumState;

enum {
    SANCTUM_MROM,
    SANCTUM_PUF,
    SANCTUM_ELFLD,
    SANCTUM_CLINT,
    SANCTUM_DRAM
};

enum {
    SANCTUM_CLOCK_FREQ = 1250000000,
};

#define SANCTUM_CPU TYPE_RISCV_CPU_RV64GCSU_V1_10_0

#endif
