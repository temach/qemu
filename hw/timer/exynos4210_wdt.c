/*
 * Samsung exynos4210 Pulse Width Modulation Timer
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "qemu/main-loop.h"
#include "hw/ptimer.h"

#include "hw/arm/exynos4210.h"

#define DEBUG_WDT

#ifdef DEBUG_WDT
#define DPRINTF(fmt, ...) \
        do { fprintf(stdout, "WDT: [%24s:%5d] " fmt, __func__, __LINE__, \
                ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define     EXYNOS4210_WDT_TIMERS_NUM      1
#define     EXYNOS4210_WDT_REG_MEM_SIZE    0xf

#define     TCFG0        0x0000
#define     TCFG1        0x0004
#define     TCON         0x0008
#define     TCNTB0       0x000C
#define     TCMPB0       0x0010
#define     TCNTO0       0x0014
#define     TCNTB1       0x0018
#define     TCMPB1       0x001C
#define     TCNTO1       0x0020
#define     TCNTB2       0x0024
#define     TCMPB2       0x0028
#define     TCNTO2       0x002C
#define     TCNTB3       0x0030
#define     TCMPB3       0x0034
#define     TCNTO3       0x0038
#define     TCNTB4       0x003C
#define     TCNTO4       0x0040
#define     TINT_CSTAT   0x0044

#define     TCNTB(x)    (0xC * (x))
#define     TCMPB(x)    (0xC * (x) + 1)
#define     TCNTO(x)    (0xC * (x) + 2)

#define GET_PRESCALER(reg) ((reg) & 0x0000ff00)
#define GET_DIVIDER(reg) ((reg) & 0x00000018)

/*
 * Attention! Timer4 doesn't have OUTPUT_INVERTER,
 * so Auto Reload bit is not accessible by macros!
 */
#define     TCON_TIMER_BASE(x)          (((x) ? 1 : 0) * 4 + 4 * (x))
#define     TCON_TIMER_START(x)         (1 << (TCON_TIMER_BASE(x) + 0))
#define     TCON_TIMER_MANUAL_UPD(x)    (1 << (TCON_TIMER_BASE(x) + 1))
#define     TCON_TIMER_OUTPUT_INV(x)    (1 << (TCON_TIMER_BASE(x) + 2))
#define     TCON_TIMER_AUTO_RELOAD(x)   (1 << (TCON_TIMER_BASE(x) + 3))
#define     TCON_TIMER4_AUTO_RELOAD     (1 << 22)

#define     TINT_CSTAT_STATUS(x)        (1 << (5 + (x)))
#define     TINT_CSTAT_ENABLE(x)        (1 << (x))

#define     WDTINT_STATUS(x)        (1 << 2)

/* timer struct */
typedef struct {
    uint32_t    id;             /* timer id */
    qemu_irq    irq;            /* local timer irq */
    uint32_t    freq;           /* timer frequency */

    /* use ptimer.c to represent count down timer */
    ptimer_state *ptimer;       /* timer  */

    /* registers */
    uint32_t    reg_tcntb;      /* counter register buffer */
    uint32_t    reg_tcmpb;      /* compare register buffer */
    uint32_t 	reg_wtcnt;

    struct Exynos4210WDTState *parent;

} Exynos4210WDT;

#define TYPE_EXYNOS4210_WDT "exynos4210.wdt"
#define EXYNOS4210_WDT(obj) \
    OBJECT_CHECK(Exynos4210WDTState, (obj), TYPE_EXYNOS4210_WDT)

typedef struct Exynos4210WDTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t    reg_tcfg[2];
    uint32_t    reg_tcon;
    uint32_t    reg_tint_cstat;

    uint32_t	reg_wtcon;
    uint32_t	reg_wtdat;
    uint32_t	reg_wtcnt;
    uint32_t	reg_wtclrint;

    Exynos4210WDT timer;

} Exynos4210WDTState;

/*** VMState ***/
static const VMStateDescription vmstate_exynos4210_wdt = {
    .name = "exynos4210.wdt.wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, Exynos4210WDT),
        VMSTATE_UINT32(freq, Exynos4210WDT),
        VMSTATE_PTIMER(ptimer, Exynos4210WDT),
        VMSTATE_UINT32(reg_tcntb, Exynos4210WDT),
        VMSTATE_UINT32(reg_tcmpb, Exynos4210WDT),
        VMSTATE_UINT32(reg_wtcnt, Exynos4210WDT),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_wdt_state = {
    .name = "exynos4210.wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg_tcfg, Exynos4210WDTState, 2),
        VMSTATE_UINT32(reg_tcon, Exynos4210WDTState),
        VMSTATE_UINT32(reg_wtcon, Exynos4210WDTState),
        VMSTATE_UINT32(reg_wtdat, Exynos4210WDTState),
        VMSTATE_UINT32(reg_wtcnt, Exynos4210WDTState),
        VMSTATE_UINT32(reg_wtclrint, Exynos4210WDTState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * WDT update frequency
 */
static void exynos4210_wdt_update_freq(Exynos4210WDTState *s, uint32_t id)
{
    DPRINTF("wdt update freq\n");
}

/*
 * Counter tick handler
 */
static void exynos4210_wdt_tick(void *opaque)
{
    DPRINTF("wdt_tick\n");
}

/*
 * WDT Read
 */
static uint64_t exynos4210_wdt_read(void *opaque, hwaddr offset,
        unsigned size)
{
    Exynos4210WDTState *s = (Exynos4210WDTState *)opaque;
    uint32_t value = 0;
    int index;

    switch (offset) {
    case TCFG0: case TCFG1:
        index = (offset - TCFG0) >> 2;
        value = s->reg_tcfg[index];
        break;

    case TCON:
        value = s->reg_tcon;
        break;

    case TCNTB0: case TCNTB1:
    case TCNTB2: case TCNTB3: case TCNTB4:
        index = (offset - TCNTB0) / 0xC;
        value = s->timer.reg_tcntb;
        break;

    case TCMPB0: case TCMPB1:
    case TCMPB2: case TCMPB3:
        index = (offset - TCMPB0) / 0xC;
        value = s->timer.reg_tcmpb;
        break;

    case TCNTO0: case TCNTO1:
    case TCNTO2: case TCNTO3: case TCNTO4:
        index = (offset == TCNTO4) ? 4 : (offset - TCNTO0) / 0xC;
        value = ptimer_get_count(s->timer.ptimer);
        break;

    case TINT_CSTAT:
        value = s->reg_tint_cstat;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "exynos4210.wdt: bad read offset " TARGET_FMT_plx,
                      offset);
        break;
    }
    return value;
}

/*
 * WDT Write
 */
static void exynos4210_wdt_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    Exynos4210WDTState *s = (Exynos4210WDTState *)opaque;
    int index;
    uint32_t new_val;
    int i;

    switch (offset) {
    case TCFG0: case TCFG1:
        index = (offset - TCFG0) >> 2;
        s->reg_tcfg[index] = value;

        /* update timers frequencies */
		exynos4210_wdt_update_freq(s, s->timer.id);
        break;

    case TCON:
		if ((value & TCON_TIMER_MANUAL_UPD(0)) >
		(s->reg_tcon & TCON_TIMER_MANUAL_UPD(0))) {
			/*
			 * TCNTB and TCMPB are loaded into TCNT and TCMP.
			 * Update timers.
			 */

			/* this will start timer to run, this ok, because
			 * during processing start bit timer will be stopped
			 * if needed */
			ptimer_set_count(s->timer.ptimer, s->timer.reg_tcntb);
			DPRINTF("set timer %d count to %x\n", i,
					s->timer.reg_tcntb);
		}

		if ((value & TCON_TIMER_START(0)) >
		(s->reg_tcon & TCON_TIMER_START(0))) {
			/* changed to start */
			ptimer_run(s->timer.ptimer, 1);
			DPRINTF("run timer %d\n", 0);
		}

		if ((value & TCON_TIMER_START(0)) <
				(s->reg_tcon & TCON_TIMER_START(0))) {
			/* changed to stop */
			ptimer_stop(s->timer.ptimer);
			DPRINTF("stop timer %d\n", 0);
		}
        s->reg_tcon = value;
        break;

    case TCNTB0: case TCNTB1:
    case TCNTB2: case TCNTB3: case TCNTB4:
        index = (offset - TCNTB0) / 0xC;
        s->timer.reg_tcntb = value;
        break;

    case TCMPB0: case TCMPB1:
    case TCMPB2: case TCMPB3:
        index = (offset - TCMPB0) / 0xC;
        s->timer.reg_tcmpb = value;
        break;

    case TINT_CSTAT:
        new_val = (s->reg_tint_cstat & 0x3E0) + (0x1F & value);
        new_val &= ~(0x3E0 & value);

		if ((new_val & TINT_CSTAT_STATUS(0)) <
				(s->reg_tint_cstat & TINT_CSTAT_STATUS(0))) {
			qemu_irq_lower(s->timer.irq);
		}
        s->reg_tint_cstat = new_val;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "exynos4210.wdt: bad write offset " TARGET_FMT_plx,
                      offset);
        break;

    }
}

/*
 * Set default values to timer fields and registers
 */
static void exynos4210_wdt_reset(DeviceState *d)
{
    DPRINTF("wdt reset\n");
}

static const MemoryRegionOps exynos4210_wdt_ops = {
    .read = exynos4210_wdt_read,
    .write = exynos4210_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * WDT timer initialization
 */
static void exynos4210_wdt_init(Object *obj)
{
    Exynos4210WDTState *s = EXYNOS4210_WDT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    int i;
    QEMUBH *bh;

	bh = qemu_bh_new(exynos4210_wdt_tick, &s->timer);
	sysbus_init_irq(dev, &s->timer.irq);
	s->timer.ptimer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
	s->timer.id = 0;
	s->timer.parent = s;

    memory_region_init_io(&s->iomem, obj, &exynos4210_wdt_ops, s,
                          "exynos4210-wdt", EXYNOS4210_WDT_REG_MEM_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void exynos4210_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_wdt_reset;
    dc->vmsd = &vmstate_exynos4210_wdt_state;
}

static const TypeInfo exynos4210_wdt_info = {
    .name          = TYPE_EXYNOS4210_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210WDTState),
    .instance_init = exynos4210_wdt_init,
    .class_init    = exynos4210_wdt_class_init,
};

static void exynos4210_wdt_register_types(void)
{
    type_register_static(&exynos4210_wdt_info);
}

type_init(exynos4210_wdt_register_types)
