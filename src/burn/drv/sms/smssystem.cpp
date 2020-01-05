#include "smsshared.h"
#include "z80_intf.h"
#include "sn76496.h"
#include "burn_ym2413.h"

bitmap_t bitmap;
cart_t cart;
input_t input;



/* Run the virtual console emulation for one frame */
void system_frame()
{
	const INT32 iline_table[] = {0xC0, 0xE0, 0xF0};
	INT32 iline;
	INT32 nSoundBufferPos = 0;

	ZetNewFrame();

	ZetOpen(0);

	/* Debounce pause key */
	if(input.system & INPUT_PAUSE)
	{
		if(!sms.paused)
		{
			sms.paused = 1;

			ZetNmi();
		}
	}
	else
	{
		sms.paused = 0;
	}

	text_counter = 0;

	vdp.lpf = (sms.display == DISPLAY_NTSC) ? 262 : 313;
	vdp.left = vdp.reg[0x0A];
	vdp.spr_col = 0xff00;

	ZetIdle(sms.cyc);
	sms.cyc = 0;

	if (pBurnSoundOut) BurnSoundClear();

	for (vdp.line = 0; vdp.line < vdp.lpf;)
	{
		iline = iline_table[vdp.extended];

		render_line(vdp.line);

		if (vdp.line <= iline)
		{
			if (--vdp.left < 0)
			{
				vdp.left = vdp.reg[0x0A];
				vdp.hint_pending = 1;

				if (vdp.reg[0x00] & 0x10)
				{
					if ((ZetTotalCycles() % CYCLES_PER_LINE) == 0)
						ZetRun(1);
					ZetSetIRQLine(0, CPU_IRQSTATUS_ACK);
				}
			}
		}

		sms.cyc += CYCLES_PER_LINE;
		ZetRun(sms.cyc - ZetTotalCycles());

		if(vdp.line == iline)
		{
			vdp.status |= 0x80;
			vdp.vint_pending = 1;

			if (vdp.reg[0x01] & 0x20)
			{
				// Check: Zool, Monster Truck Wars, Chicago Syndacite, Terminator 2 (SMS)
				ZetSetIRQLine(0, CPU_IRQSTATUS_ACK);
			}
		}

		// Render Sound Segment
		if (pBurnSoundOut && sms.use_fm) {
			INT32 nSegmentLength = nBurnSoundLen / vdp.lpf;
			INT16* pSoundBuf = pBurnSoundOut + (nSoundBufferPos << 1);
			if (sms.use_fm)	{
				BurnYM2413Render(pSoundBuf, nSegmentLength);
			}
			nSoundBufferPos += nSegmentLength;
		}

		vdp.line++;
	}

	sms.cyc = ZetTotalCycles() - sms.cyc;

	// Make sure the buffer is entirely filled.
	if (pBurnSoundOut) {
		INT32 nSegmentLength = nBurnSoundLen - nSoundBufferPos;
		INT16* pSoundBuf = pBurnSoundOut + (nSoundBufferPos << 1);
		if (nSegmentLength && sms.use_fm) {
			BurnYM2413Render(pSoundBuf, nSegmentLength);
		}
		SN76496Update(pBurnSoundOut, nBurnSoundLen);
	}

	ZetClose();
}

void system_init(void)
{
	sms_init();
	pio_init();
	vdp_init();
	render_init();
	sound_init();

	sms.save = 0;
}

void system_shutdown(void)
{
	sms_shutdown();
	pio_shutdown();
	vdp_shutdown();
	render_shutdown();
	sound_shutdown();
}

void system_reset(void)
{
	sms_reset();
	pio_reset();
	vdp_reset();
	render_reset();
	sound_reset();
	//    system_manage_sram(cart.sram, SLOT_CART, SRAM_LOAD);
}

void system_poweron(void)
{
	system_reset();
}

void system_poweroff(void)
{
	//    system_manage_sram(cart.sram, SLOT_CART, SRAM_SAVE);
}

