/*
 * Samsung exynos4210 Watchdog Timer
 *
 * Artem Abramov <tematibr@gmail.com>
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

#include "include/sysemu/sysemu.h"

#define DEBUG_WDT

#ifdef DEBUG_WDT
#define DPRINTF(fmt, ...) \
        do { fprintf(stdout, "WDT: [%24s:%5d] " fmt, __func__, __LINE__, \
                ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define     EXYNOS4210_WDT_REG_MEM_SIZE    0xf

#define		WTCON		0x0000
#define		WTDAT		0x0004
#define		WTCNT		0x0008
#define		WTCLRINT	0x000C

#define 	WTCON_WDT_ENABLE	(1 << 5)
#define 	WTCON_IRQ_ENABLE 	(1 << 2)
#define 	WTCON_RST_ENABLE 	(1 << 0)

#define GET_PRESCALER(reg) ((reg) & 0x0000ff00)
#define GET_DIVIDER(reg) ((reg) & 0x00000018)

/* timer struct */
typedef struct {
    qemu_irq    irq;            /* local timer irq */
    uint32_t    freq;           /* timer frequency */

    /* use ptimer.c to represent count down timer */
    ptimer_state *ptimer;       /* timer  */

    /* registers */
    uint32_t 	reg_wtdat;		/* watchdog register data */
    uint32_t	reg_wtcnt;

    struct Exynos4210WDTState *parent;

} Exynos4210WDT;

#define TYPE_EXYNOS4210_WDT "exynos4210.wdt"
#define EXYNOS4210_WDT(obj) \
    OBJECT_CHECK(Exynos4210WDTState, (obj), TYPE_EXYNOS4210_WDT)

typedef struct Exynos4210WDTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t	reg_wtcon;
    uint32_t	reg_wtclrint;

    Exynos4210WDT timer;

} Exynos4210WDTState;

/*** VMState ***/
static const VMStateDescription vmstate_exynos4210_wdt = {
    .name = "exynos4210.wdt.wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(freq, Exynos4210WDT),
        VMSTATE_PTIMER(ptimer, Exynos4210WDT),
        VMSTATE_UINT32(reg_wtcnt, Exynos4210WDT),
        VMSTATE_UINT32(reg_wtdat, Exynos4210WDT),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_wdt_state = {
    .name = "exynos4210.wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(reg_wtcon, Exynos4210WDTState),
        VMSTATE_UINT32(reg_wtclrint, Exynos4210WDTState),
        VMSTATE_STRUCT(timer, Exynos4210WDTState, 0
        		, vmstate_exynos4210_wdt, Exynos4210WDT),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * WDT update frequency
 */
static void exynos4210_wdt_update_freq(Exynos4210WDTState *s)
{
    DPRINTF("wdt update freq\n");
    uint32_t freq;
    freq = s->timer.freq;
    uint32_t divider = 16;
    switch(GET_DIVIDER(s->reg_wtcon)) {
		case 0x18:
			divider = 128;
		case 0x10:
			divider = 64;
		case 0x08:
			divider = 32;
		default:
			divider = 16;
    }
    s->timer.freq = 24000000 / ((GET_PRESCALER(s->reg_wtcon) + 1) * divider);
    if (freq != s->timer.freq) {
        ptimer_set_freq(s->timer.ptimer, s->timer.freq);
    }
	DPRINTF("freq=%dHz\n", s->timer.freq);
}

/*
 * Counter tick handler
 */
static void exynos4210_wdt_tick(void *opaque)
{
    DPRINTF("wdt_tick, REBOOTING\n");
    Exynos4210WDT *s = (Exynos4210WDT *)opaque;
    Exynos4210WDTState *p = (Exynos4210WDTState *)s->parent;

    if (p->reg_wtcon & WTCON_IRQ_ENABLE) {
		DPRINTF("raise interrupt\n");
		qemu_irq_raise(p->timer.irq);
		ptimer_set_count(p->timer.ptimer, p->timer.reg_wtdat);
		ptimer_run(p->timer.ptimer, 1);
    }
    else if (p->reg_wtcon & WTCON_RST_ENABLE) {
		/* reboot machine */
		qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

/*
 * WDT Read
 */
static uint64_t exynos4210_wdt_read(void *opaque, hwaddr offset,
        unsigned size)
{
    DPRINTF("wdt read\n");
    Exynos4210WDTState *s = (Exynos4210WDTState *)opaque;
    uint32_t value = 0;

    switch (offset) {
    case WTCON:
        value = s->reg_wtcon;
        break;

    case WTDAT:
        value = s->timer.reg_wtdat;
        break;

    case WTCNT:
        value = ptimer_get_count(s->timer.ptimer);
		DPRINTF("read WTCNT as %x\n", value);
        break;

    case WTCLRINT:
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
    DPRINTF("wdt write\n");
    Exynos4210WDTState *s = (Exynos4210WDTState *)opaque;

    switch (offset) {
    case WTCON:
		if ((value & WTCON_WDT_ENABLE) >
			(s->reg_wtcon & WTCON_WDT_ENABLE)){
			/* changed to start */
			ptimer_set_count(s->timer.ptimer, s->timer.reg_wtdat);
			ptimer_run(s->timer.ptimer, 1);
			DPRINTF("run timer\n");
		}

		if ((value & WTCON_WDT_ENABLE) <
			(s->reg_wtcon & WTCON_WDT_ENABLE)) {
			/* changed to stop */
			ptimer_stop(s->timer.ptimer);
			DPRINTF("stop timer\n");
		}
        s->reg_wtcon = value;
        /* update timers frequencies */
		exynos4210_wdt_update_freq(s);
        break;

    case WTDAT:
        s->timer.reg_wtdat = value;
        break;

    case WTCNT:
		/* this will start timer to run, this ok, because
		 * during processing start bit timer will be stopped
		 * if needed */
		ptimer_set_count(s->timer.ptimer, value);
		if (! (s->reg_wtcon & WTCON_WDT_ENABLE)) {
			/* timer is disabled */
			ptimer_stop(s->timer.ptimer);
			DPRINTF("keep timer stopped\n");
		}
		DPRINTF("set timer count to %lx\n", value);
        s->timer.reg_wtcnt = value;
        break;

    case WTCLRINT:
		qemu_irq_lower(s->timer.irq);
		DPRINTF("clear timer interrupt\n");
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
    Exynos4210WDTState *s = EXYNOS4210_WDT(d);
    s->reg_wtcon 		= 0x00008021;
	s->timer.reg_wtdat 	= 0x00008000;
	s->timer.reg_wtcnt 	= 0x00008000;
	s->reg_wtclrint 	= 0;

	exynos4210_wdt_update_freq(s);
	ptimer_stop(s->timer.ptimer);
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
    QEMUBH *bh;

	bh = qemu_bh_new(exynos4210_wdt_tick, &s->timer);
	sysbus_init_irq(dev, &s->timer.irq);
	s->timer.ptimer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
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
