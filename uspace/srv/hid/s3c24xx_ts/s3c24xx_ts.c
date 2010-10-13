/*
 * Copyright (c) 2010 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup mouse
 * @{
 */
/**
 * @file
 * @brief Samsung Samsung S3C24xx on-chip ADC and touch-screen interface driver.
 *
 * This interface is present on the Samsung S3C24xx CPU (on the gta02 platform).
 */

#include <ddi.h>
#include <libarch/ddi.h>
#include <devmap.h>
#include <io/console.h>
#include <vfs/vfs.h>
#include <ipc/ipc.h>
#include <ipc/mouse.h>
#include <async.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysinfo.h>
#include <errno.h>

#include "s3c24xx_ts.h"

#define NAME "s3c24ser"
#define NAMESPACE "hid_in"

static irq_cmd_t ts_irq_cmds[] = {
	{
		.cmd = CMD_ACCEPT
	}
};

static irq_code_t ts_irq_code = {
	sizeof(ts_irq_cmds) / sizeof(irq_cmd_t),
	ts_irq_cmds
};

/** S3C24xx touchscreen instance structure */
static s3c24xx_ts_t *ts;

static void s3c24xx_ts_connection(ipc_callid_t iid, ipc_call_t *icall);
static void s3c24xx_ts_irq_handler(ipc_callid_t iid, ipc_call_t *call);
static void s3c24xx_ts_pen_down(s3c24xx_ts_t *ts);
static void s3c24xx_ts_pen_up(s3c24xx_ts_t *ts);
static void s3c24xx_ts_eoc(s3c24xx_ts_t *ts);
static int s3c24xx_ts_init(s3c24xx_ts_t *ts);
static void s3c24xx_ts_wait_for_int_mode(s3c24xx_ts_t *ts, ts_updn_t updn);
static void s3c24xx_ts_convert_samples(int smp0, int smp1, int *x, int *y);
static int lin_map_range(int v, int i0, int i1, int o0, int o1);

int main(int argc, char *argv[])
{
	int rc;

	printf(NAME ": S3C24xx touchscreen driver\n");

	rc = devmap_driver_register(NAME, s3c24xx_ts_connection);
	if (rc < 0) {
		printf(NAME ": Unable to register driver.\n");
		return -1;
	}

	ts = malloc(sizeof(s3c24xx_ts_t));
	if (ts == NULL)
		return -1;

	if (s3c24xx_ts_init(ts) != EOK)
		return -1;

	rc = devmap_device_register(NAMESPACE "/mouse", &ts->dev_handle);
	if (rc != EOK) {
		devmap_hangup_phone(DEVMAP_DRIVER);
		printf(NAME ": Unable to register device %s.\n",
		    NAMESPACE "/mouse");
		return -1;
	}

	printf(NAME ": Registered device %s.\n", NAMESPACE "/mouse");

	printf(NAME ": Accepting connections\n");
	task_retval(0);
	async_manager();

	/* Not reached */
	return 0;
}

/** Initialize S3C24xx touchscreen interface. */
static int s3c24xx_ts_init(s3c24xx_ts_t *ts)
{
	void *vaddr;
	sysarg_t inr;

	inr = S3C24XX_TS_INR;
	ts->paddr = S3C24XX_TS_ADDR;

	if (pio_enable((void *) ts->paddr, sizeof(s3c24xx_adc_io_t),
	    &vaddr) != 0)
		return -1;

	ts->io = vaddr;
	ts->client_phone = -1;
	ts->state = ts_wait_pendown;
	ts->last_x = 0;
	ts->last_y = 0;

	printf(NAME ": device at physical address 0x%x, inr %d.\n",
	    ts->paddr, inr);

	async_set_interrupt_received(s3c24xx_ts_irq_handler);
	ipc_register_irq(inr, device_assign_devno(), 0, &ts_irq_code);

	s3c24xx_ts_wait_for_int_mode(ts, updn_down);

	return EOK;
}

/** Switch interface to wait for interrupt mode.
 *
 * In this mode we receive an interrupt when pen goes up/down, depending
 * on @a updn.
 *
 * @param ts	Touchscreen instance
 * @param updn	@c updn_up to wait for pen up, @c updn_down to wait for pen
 *		down.
 */
static void s3c24xx_ts_wait_for_int_mode(s3c24xx_ts_t *ts, ts_updn_t updn)
{
	uint32_t con, tsc;

	/*
	 * Configure ADCCON register
	 */

	con = pio_read_32(&ts->io->con);

	/* Disable standby, disable start-by-read, clear manual start bit */
	con = con & ~(ADCCON_STDBM | ADCCON_READ_START | ADCCON_ENABLE_START);

	/* Set prescaler value 0xff, XP for input. */
	con = con | (ADCCON_PRSCVL(0xff) << 6) | ADCCON_SEL_MUX(SMUX_XP);

	/* Enable prescaler. */
	con = con | ADCCON_PRSCEN;

 	pio_write_32(&ts->io->con, con);

	/*
	 * Configure ADCTSC register
	 */

	tsc = pio_read_32(&ts->io->tsc);

	/* Select whether waiting for pen up or pen down. */
	if (updn == updn_up)
		tsc |= ADCTSC_DSUD_UP;
	else
		tsc &= ~ADCTSC_DSUD_UP;

	/*
	 * Enable XP pull-up and disable all drivers except YM. This is
	 * according to the manual. This gives us L on XP input when touching
	 * and (pulled up to) H when not touching.
	 */
	tsc = tsc & ~(ADCTSC_XM_ENABLE | ADCTSC_AUTO_PST |
	    ADCTSC_PULLUP_DISABLE);
	tsc = tsc | ADCTSC_YP_DISABLE | ADCTSC_XP_DISABLE | ADCTSC_YM_ENABLE;

	/* Select wait-for-interrupt mode. */
	tsc = (tsc & ~ADCTSC_XY_PST_MASK) | ADCTSC_XY_PST_WAITINT;

	pio_write_32(&ts->io->tsc, tsc);
}

/** Handle touchscreen interrupt */
static void s3c24xx_ts_irq_handler(ipc_callid_t iid, ipc_call_t *call)
{
	ts_updn_t updn;

	(void) iid; (void) call;

	/* Read up/down interrupt flags. */
	updn = pio_read_32(&ts->io->updn);

	if (updn & (ADCUPDN_TSC_DN | ADCUPDN_TSC_UP)) {
		/* Clear up/down interrupt flags. */
		pio_write_32(&ts->io->updn, updn &
		    ~(ADCUPDN_TSC_DN | ADCUPDN_TSC_UP));
	}

	if (updn & ADCUPDN_TSC_DN) {
		/* Pen-down interrupt */
		s3c24xx_ts_pen_down(ts);
	} else if (updn & ADCUPDN_TSC_UP) {
		/* Pen-up interrupt */
		s3c24xx_ts_pen_up(ts);
	} else {
		/* Presumably end-of-conversion interrupt */

		/* Check end-of-conversion flag. */
		if ((pio_read_32(&ts->io->con) & ADCCON_ECFLG) == 0) {
			printf(NAME ": Unrecognized ts int.\n");
			return;
		}

		if (ts->state != ts_sample_pos) {
			/*
			 * We got an extra interrupt ater switching to
			 * wait for interrupt mode.
			 */
			return;
		}

		/* End-of-conversion interrupt */
		s3c24xx_ts_eoc(ts);
	}
}

/** Handle pen-down interrupt.
 *
 * @param ts	Touchscreen instance
 */
static void s3c24xx_ts_pen_down(s3c24xx_ts_t *ts)
{
	/* Pen-down interrupt */

	ts->state = ts_sample_pos;

	/* Enable auto xy-conversion mode */
	pio_write_32(&ts->io->tsc, (pio_read_32(&ts->io->tsc)
	    & ~3) | 4);

	/* Start the conversion. */
	pio_write_32(&ts->io->con, pio_read_32(&ts->io->con)
	    | ADCCON_ENABLE_START);
}

/** Handle pen-up interrupt.
 *
 * @param ts	Touchscreen instance
 */
static void s3c24xx_ts_pen_up(s3c24xx_ts_t *ts)
{
	int button, press;

	/* Pen-up interrupt */

	ts->state = ts_wait_pendown;

	button = 1;
	press = 0;
	async_msg_2(ts->client_phone, MEVENT_BUTTON, button, press);

	s3c24xx_ts_wait_for_int_mode(ts, updn_down);
}

/** Handle end-of-conversion interrupt.
 *
 * @param ts	Touchscreen instance
 */
static void s3c24xx_ts_eoc(s3c24xx_ts_t *ts)
{
	uint32_t data;
	int button, press;
	int smp0, smp1;
	int x_pos, y_pos;
	int dx, dy;

	ts->state = ts_wait_penup;

	/* Read in sampled data. */

	data = pio_read_32(&ts->io->dat0);
	smp0 = data & 0x3ff;

	data = pio_read_32(&ts->io->dat1);
	smp1 = data & 0x3ff;

	/* Convert to screen coordinates. */
	s3c24xx_ts_convert_samples(smp0, smp1, &x_pos, &y_pos);

	printf("s0: 0x%03x, s1:0x%03x -> x:%d,y:%d\n", smp0, smp1,
	    x_pos, y_pos);

	/* Get differences. */
	dx = x_pos - ts->last_x;
	dy = y_pos - ts->last_y;

	button = 1;
	press = 1;

	/* Send notifications to client. */
	async_msg_2(ts->client_phone, MEVENT_MOVE, dx, dy);
	async_msg_2(ts->client_phone, MEVENT_BUTTON, button, press);

	ts->last_x = x_pos;
	ts->last_y = y_pos;

	s3c24xx_ts_wait_for_int_mode(ts, updn_up);
}

/** Convert sampled data to screen coordinates. */
static void s3c24xx_ts_convert_samples(int smp0, int smp1, int *x, int *y)
{
	/*
	 * The orientation and display dimensions are GTA02-specific and the
	 * calibration values might even specific to the individual piece
	 * of hardware.
	 *
	 * The calibration values can be obtained by touching corners
	 * of the screen with the stylus and noting the sampled values.
	 */
	*x = lin_map_range(smp1, 0xa1, 0x396, 0, 479);
	*y = lin_map_range(smp0, 0x69, 0x38a, 639, 0);
}

/** Map integer from one range to another range in a linear fashion.
 *
 * i0 < i1 is required. i0 is mapped to o0, i1 to o1. If o1 < o0, then the
 * mapping will be descending. If v is outside of [i0, i1], it is clamped.
 *
 * @param v	Value to map.
 * @param i0	Lower bound of input range.
 * @param i1	Upper bound of input range.
 * @param o0	First bound of output range.
 * @param o1	Second bound of output range.
 *
 * @return	Mapped value ov, o0 <= ov <= o1.
 */
static int lin_map_range(int v, int i0, int i1, int o0, int o1)
{
	if (v < i0)
		v = i0;

	if (v > i1)
		v = i1;

	return o0 + (o1 - o0) * (v - i0) / (i1 - i0);
}

/** Handle mouse client connection. */
static void s3c24xx_ts_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	ipc_callid_t callid;
	ipc_call_t call;
	int retval;

	ipc_answer_0(iid, EOK);

	while (1) {
		callid = async_get_call(&call);
		switch (IPC_GET_METHOD(call)) {
		case IPC_M_PHONE_HUNGUP:
			if (ts->client_phone != -1) {
				ipc_hangup(ts->client_phone);
				ts->client_phone = -1;
			}

			ipc_answer_0(callid, EOK);
			return;
		case IPC_M_CONNECT_TO_ME:
			if (ts->client_phone != -1) {
				retval = ELIMIT;
				break;
			}
			ts->client_phone = IPC_GET_ARG5(call);
			retval = 0;
			break;
		default:
			retval = EINVAL;
		}
		ipc_answer_0(callid, retval);
	}
}

/** @}
 */