/*
 *  sid.cpp - 6581 SID emulation
 *
 *  Frodo (C) Copyright 1994-2004 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sys.h"

#ifndef SID_PLAYER
#include <SDL.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef __linux__
// Catweasel ioctls (included here for convenience)
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#define CWSID_IOCTL_TYPE ('S')
#define CWSID_IOCTL_RESET        _IO(CWSID_IOCTL_TYPE, 0)
#define CWSID_IOCTL_CARDTYPE	 _IOR(CWSID_IOCTL_TYPE, 4, int)
#define CWSID_IOCTL_PAL          _IO(CWSID_IOCTL_TYPE, 0x11)
#define CWSID_IOCTL_NTSC         _IO(CWSID_IOCTL_TYPE, 0x12)
#define CWSID_IOCTL_DOUBLEBUFFER _IOW(CWSID_IOCTL_TYPE, 0x21, int)
#define CWSID_IOCTL_DELAY        _IOW(CWSID_IOCTL_TYPE, 0x22, int)
#define CWSID_MAGIC 0x100
#define HAVE_CWSID 1
#endif

#include "sid.h"
#include "main.h"
#include "prefs.h"

#ifdef SID_PLAYER
#include "mem.h"
#include "cpu.h"
#else
#include "util.h"
#include "snapshot.h"
#endif

#define DEBUG 0
#include "debug.h"


#undef USE_FIXPOINT_MATHS


// Filter math definitions
#ifdef USE_FIXPOINT_MATHS
typedef int32 filt_t;
const int32 F_ONE = 0x10000;
const int32 F_ZERO = 0;
#else
typedef float filt_t;
const float F_ONE = 1.0;
const float F_ZERO = 0.0;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Desired and obtained audio formats
static SDL_AudioSpec desired, obtained;

// Flag: emulate SID filters
static bool enable_filters = true;

// Flag: emulate 2 SID chips
static bool dual_sid = false;

// Flag: emulate new SID chip (8580)
static bool emulate_8580 = false;

// Audio effect type (0 = none, 1 = reverb, 2 = spatial)
static int audio_effect = 0;

// Master volume (0..0x100)
static int32 master_volume;

// Volumes (0..0x100) and panning (-0x100..0x100) of voices 1..4 (both SIDs)
static int32 v1_volume, v2_volume, v3_volume, v4_volume;
static int32 v1_panning, v2_panning, v3_panning, v4_panning;

// Dual-SID stereo separation (0..0x100)
static int32 dual_sep;

// Number of SID clocks per sample frame
static uint32 sid_cycles;		// Integer
static filt_t sid_cycles_frac;	// With fractional part

#ifdef SID_PLAYER
// Phi2 clock frequency
static cycle_t cycles_per_second;
const float PAL_CLOCK = 985248.444;
const float NTSC_OLD_CLOCK = 1000000.0;
const float NTSC_CLOCK = 1022727.143;

// Replay counter variables
static uint16 cia_timer;		// CIA timer A latch
static int replay_count;		// Counter for timing replay routine
static int speed_adjust;		// Speed adjustment in percent

// Clock frequency changed
void SIDClockFreqChanged();
#endif

// Catweasel device file handle
static int cwsid_fh = -1;

// Resonance frequency polynomials
static inline float CALC_RESONANCE_LP(float f)
{
	return 227.755 - 1.7635 * f - 0.0176385 * f * f + 0.00333484 * f * f * f;
}

static inline float CALC_RESONANCE_HP(float f)
{
	return 366.374 - 14.0052 * f + 0.603212 * f * f - 0.000880196 * f * f * f;
}

// Pseudo-random number generator for SID noise waveform (don't use f_rand()
// because the SID waveform calculation runs asynchronously and the output of
// f_rand() has to be predictable inside the main emulation)
static uint32 noise_rand_seed = 1;

inline static uint8 noise_rand()
{
	// This is not the original SID noise algorithm (which is unefficient to
	// implement in software) but this sounds close enough
	noise_rand_seed = noise_rand_seed * 1103515245 + 12345;
	return noise_rand_seed >> 16;
}

// SID waveforms
enum {
	WAVE_NONE,
	WAVE_TRI,
	WAVE_SAW,
	WAVE_TRISAW,
	WAVE_RECT,
	WAVE_TRIRECT,
	WAVE_SAWRECT,
	WAVE_TRISAWRECT,
	WAVE_NOISE
};

// EG states
enum {
	EG_IDLE,
	EG_ATTACK,
	EG_DECAY,
	EG_RELEASE
};

// Filter types
enum {
	FILT_NONE,
	FILT_LP,
	FILT_BP,
	FILT_LPBP,
	FILT_HP,
	FILT_NOTCH,
	FILT_HPBP,
	FILT_ALL
};

// Voice 4 states (SIDPlayer only)
enum {
	V4_OFF,
	V4_GALWAY_NOISE,
	V4_SAMPLE
};

// Structure for one voice
struct voice_t {
	int wave;			// Selected waveform
	int eg_state;		// Current state of EG
	voice_t *mod_by;	// Voice that modulates this one
	voice_t *mod_to;	// Voice that is modulated by this one

	uint32 count;		// Counter for waveform generator, 8.16 fixed
	uint32 add;			// Added to counter in every sample frame

	uint16 freq;		// SID frequency value
	uint16 pw;			// SID pulse-width value

	uint32 a_add;		// EG parameters
	uint32 d_sub;
	uint32 s_level;
	uint32 r_sub;
	uint32 eg_level;	// Current EG level, 8.16 fixed

	uint32 noise;		// Last noise generator output value

	uint16 left_gain;	// Gain on left channel (12.4 fixed)
	uint16 right_gain;	// Gain on right channel (12.4 fixed)

	bool gate;			// EG gate bit
	bool ring;			// Ring modulation bit
	bool test;			// Test bit
	bool filter;		// Flag: Voice filtered

						// The following bit is set for the modulating
						// voice, not for the modulated one (as the SID bits)
	bool sync;			// Sync modulation bit
	bool mute;			// Voice muted (voice 3 only)
};

// Data structures for both SIDs
struct sid_t {
	sid_t(int num);

	void reset();
	uint32 read(uint32 adr, cycle_t now);
	void write(uint32 adr, uint32 byte, cycle_t now, bool rmw);
	void calc_filter();
	void calc_gains(bool is_left_sid, bool is_right_sid);
	void calc_gain_voice(int32 volume, int32 panning, uint16 &left_gain, uint16 &right_gain);
	void chunk_read(size_t size);
	void chunk_write();

	int sid_num;						// SID number (0 or 1)

	voice_t voice[3];					// Data for 3 voices

	uint8 regs[128];					// Copies of the 25 write-only SID registers (SIDPlayer uses 128 registers)
	uint8 last_written_byte;			// Byte last written to SID (for emulation of read accesses to write-only registers)
	uint8 volume;						// Master volume (0..15)

	uint8 f_type;						// Filter type
	uint8 f_freq;						// SID filter frequency (upper 8 bits)
	uint8 f_res;						// Filter resonance (0..15)

	filt_t f_ampl;						// IIR filter input attenuation
	filt_t d1, d2, g1, g2;				// IIR filter coefficients
	filt_t xn1_l, xn2_l, yn1_l, yn2_l;	// IIR filter previous input/output signal (left and right channel)
	filt_t xn1_r, xn2_r, yn1_r, yn2_r;

	uint16 v4_left_gain;				// Gain of voice 4 on left channel (12.4 fixed)
	uint16 v4_right_gain;				// Gain of voice 4 on right channel (12.4 fixed)

#ifdef SID_PLAYER
	int v4_state;						// State of voice 4 (Galway noise/samples)
	uint32 v4_count;					// Counter for voice 4
	uint32 v4_add;						// Added to counter in every frame

	uint16 gn_adr;						// C64 address of tone list
	uint16 gn_tone_length;				// Length of each tone in samples
	uint32 gn_volume_add;				// Added to SID volume reg. for every sample
	int	gn_tone_counter;				// Number of tones in list
	uint16 gn_base_cycles;				// Cycles before sample
	uint16 gn_loop_cycles;				// Cycles between samples
	uint32 gn_last_count;				// Value of v4_count (lower 16 bits cleared) at end of tone

	uint32 sm_adr;						// C64 nybble address of sample
	uint32 sm_end_adr;					// C64 nybble address of end of sample
	uint32 sm_rep_adr;					// C64 nybble address of sample repeat point
	uint16 sm_volume;					// Sample volume (0..2, 0=loudest)
	uint8 sm_rep_count;					// Sample repeat counter (0xff=continous)
	bool sm_big_endian;					// Flag: Sample is big-endian
#endif
} sid1(0), sid2(1);

// Waveform tables
static uint16 tri_table[0x1000*2];
static const uint16 *tri_saw_table;
static const uint16 *tri_rect_table;
static const uint16 *saw_rect_table;
static const uint16 *tri_saw_rect_table;

// Sampled from a 6581R4
static const uint16 tri_saw_table_6581[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0808,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1010, 0x3C3C,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0808,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1010, 0x3C3C
};

static const uint16 tri_rect_table_6581[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x8080,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x8080,
	0, 0, 0, 0, 0, 0, 0x8080, 0xC0C0, 0, 0x8080, 0x8080, 0xE0E0, 0x8080, 0xE0E0, 0xF0F0, 0xFCFC,
	0xFFFF, 0xFCFC, 0xFAFA, 0xF0F0, 0xF6F6, 0xE0E0, 0xE0E0, 0x8080, 0xEEEE, 0xE0E0, 0xE0E0, 0x8080, 0xC0C0, 0, 0, 0,
	0xDEDE, 0xC0C0, 0xC0C0, 0, 0x8080, 0, 0, 0, 0x8080, 0, 0, 0, 0, 0, 0, 0,
	0xBEBE, 0x8080, 0x8080, 0, 0x8080, 0, 0, 0, 0x8080, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x7E7E, 0x4040, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16 saw_rect_table_6581[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x7878,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x7878
};

static const uint16 tri_saw_rect_table_6581[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Sampled from an 8580R5
static const uint16 tri_saw_table_8580[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0808,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1818, 0x3C3C,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1C1C,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x8080, 0, 0x8080, 0x8080,
	0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xE0E0, 0xF0F0, 0xF0F0, 0xF0F0, 0xF0F0, 0xF8F8, 0xF8F8, 0xFCFC, 0xFEFE
};

static const uint16 tri_rect_table_8580[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0xFFFF, 0xFCFC, 0xF8F8, 0xF0F0, 0xF4F4, 0xF0F0, 0xF0F0, 0xE0E0, 0xECEC, 0xE0E0, 0xE0E0, 0xC0C0, 0xE0E0, 0xC0C0, 0xC0C0, 0xC0C0,
	0xDCDC, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0x8080, 0x8080, 0xC0C0, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0, 0,
	0xBEBE, 0xA0A0, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0, 0x8080, 0x8080, 0, 0, 0, 0, 0, 0,
	0x8080, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x7E7E, 0x7070, 0x6060, 0, 0x4040, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16 saw_rect_table_8580[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x8080,
	0, 0, 0, 0, 0, 0, 0x8080, 0x8080, 0, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0xB0B0, 0xBEBE,
	0, 0, 0, 0, 0, 0, 0, 0x8080, 0, 0, 0, 0x8080, 0x8080, 0x8080, 0x8080, 0xC0C0,
	0, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0xC0C0, 0x8080, 0x8080, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xDCDC,
	0x8080, 0x8080, 0x8080, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xE0E0, 0xE0E0, 0xE0E0, 0xE0E0, 0xECEC,
	0xC0C0, 0xE0E0, 0xE0E0, 0xE0E0, 0xE0E0, 0xF0F0, 0xF0F0, 0xF4F4, 0xF0F0, 0xF0F0, 0xF8F8, 0xF8F8, 0xF8F8, 0xFCFC, 0xFEFE, 0xFFFF
};

static const uint16 tri_saw_rect_table_8580[0x100] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x8080, 0x8080,
	0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0xC0C0, 0xC0C0, 0xC0C0, 0xC0C0, 0xE0E0, 0xE0E0, 0xE0E0, 0xF0F0, 0xF8F8, 0xFCFC
};

// Envelope tables
static uint32 eg_table[16];

static const uint8 eg_dr_shift[256] = {
	5,5,5,5,5,5,5,5,4,4,4,4,4,4,4,4,
	3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// Filter tables
static filt_t ffreq_lp[256];	// Low-pass resonance frequency table
static filt_t ffreq_hp[256];	// High-pass resonance frequency table

// Table for sampled voices
#ifdef SID_PLAYER
static const int16 sample_tab[16 * 3] = {
	0x8000, 0x9111, 0xa222, 0xb333, 0xc444, 0xd555, 0xe666, 0xf777,
	0x0888, 0x1999, 0x2aaa, 0x3bbb, 0x4ccc, 0x5ddd, 0x6eee, 0x7fff,

	0xc444, 0xc444, 0xd555, 0xd555, 0xe666, 0xe666, 0xf777, 0xf777,
	0x0888, 0x0888, 0x1999, 0x1999, 0x2aaa, 0x2aaa, 0x3bbb, 0x3bbb,

	0xe666, 0xe666, 0xe666, 0xe666, 0xf777, 0xf777, 0xf777, 0xf777,
	0x0888, 0x0888, 0x0888, 0x0888, 0x1999, 0x1999, 0x1999, 0x1999
};

static int16 galway_tab[16 * 64];
#else
static const int16 sample_tab[16] = {
	0x8000, 0x9111, 0xa222, 0xb333, 0xc444, 0xd555, 0xe666, 0xf777,
	0x0888, 0x1999, 0x2aaa, 0x3bbb, 0x4ccc, 0x5ddd, 0x6eee, 0x7fff
};
#endif

// Work buffer and variables for audio effects
const int WORK_BUFFER_SIZE = 0x10000;
static int16 work_buffer[WORK_BUFFER_SIZE];
static int wb_read_offset = 0, wb_write_offset = 0;
static int rev_feedback = 0;

// Prototypes
static void calc_buffer(void *userdata, uint8 *buf, int count);
static void sid1_chunk_read(size_t size);
static bool sid1_chunk_write();
static void sid2_chunk_read(size_t size);
static bool sid2_chunk_write();


/*
 *  Init SID emulation
 */

sid_t::sid_t(int n) : sid_num(n)
{
	// Link voices
	voice[0].mod_by = &voice[2];
	voice[1].mod_by = &voice[0];
	voice[2].mod_by = &voice[1];
	voice[0].mod_to = &voice[1];
	voice[1].mod_to = &voice[2];
	voice[2].mod_to = &voice[0];

	reset();
}

static void set_desired_samples(int32 sample_rate)
{
	if (sample_rate < 15000)
		desired.samples = 256;
	else if (sample_rate < 30000)
		desired.samples = 512;
	else
		desired.samples = 1024;
#ifdef SID_PLAYER
	// Music replay doesn't need low latency
	desired.samples *= 8;
#endif
}

static void set_rev_delay(int32 delay_ms)
{
	int delay = (delay_ms * obtained.freq / 1000) & ~1;
	if (delay == 0)
		delay = 2;
	wb_read_offset = (wb_write_offset - delay) & (WORK_BUFFER_SIZE - 1);
}

static void calc_gains()
{
	if (dual_sid) {
		sid1.calc_gains(true, false);
		sid2.calc_gains(false, true);
	} else
		sid1.calc_gains(false, false);
}

static void set_sid_data()
{
	if (emulate_8580) {
		tri_saw_table = tri_saw_table_8580;
		tri_rect_table = tri_rect_table_8580;
		saw_rect_table = saw_rect_table_8580;
		tri_saw_rect_table = tri_saw_rect_table_8580;
	} else {
		tri_saw_table = tri_saw_table_6581;
		tri_rect_table = tri_rect_table_6581;
		saw_rect_table = saw_rect_table_6581;
		tri_saw_rect_table = tri_saw_rect_table_6581;
	}
}

static void prefs_sidtype_changed(const char *name, const char *from, const char *to)
{
	emulate_8580 = (strncmp(to, "8580", 4) == 0);
	set_sid_data();
}

static void prefs_samplerate_changed(const char *name, int32 from, int32 to)
{
	SDL_CloseAudio();
	desired.freq = obtained.freq = to;
	set_desired_samples(to);
	SDL_OpenAudio(&desired, &obtained);
	SIDClockFreqChanged();
	set_rev_delay(PrefsFindInt32("revdelay"));
	SDL_PauseAudio(false);
}

static void prefs_audio16bit_changed(const char *name, bool from, bool to)
{
	SDL_CloseAudio();
	desired.format = obtained.format = to ? AUDIO_S16SYS : AUDIO_U8;
	SDL_OpenAudio(&desired, &obtained);
	SDL_PauseAudio(false);
}

static void prefs_stereo_changed(const char *name, bool from, bool to)
{
	SDL_CloseAudio();
	desired.channels = obtained.channels = to ? 2 : 1;
	SDL_OpenAudio(&desired, &obtained);
	SDL_PauseAudio(false);
}

static void prefs_filters_changed(const char *name, bool from, bool to)
{
	SDL_LockAudio();
	if (!from && to) {
		sid1.calc_filter();
		sid2.calc_filter();
	}
	enable_filters = to;
	SDL_UnlockAudio();
}

static void prefs_dualsid_changed(const char *name, bool from, bool to)
{
	SDL_LockAudio();
	if (!from && to)
		sid2.reset();
	dual_sid = to;
	calc_gains();
	SDL_UnlockAudio();
}

static void prefs_audioeffect_changed(const char *name, int32 from, int32 to)
{
	if (to)
		memset(work_buffer, 0, sizeof(work_buffer));
	audio_effect = to;
}

static void prefs_revdelay_changed(const char *name, int32 from, int32 to)
{
	set_rev_delay(to);
}

static void prefs_revfeedback_changed(const char *name, int32 from, int32 to)
{
	rev_feedback = to;
}

static void prefs_volume_changed(const char *name, int32 from, int32 to)
{
	master_volume = to;
	calc_gains();
}

static void prefs_v1volume_changed(const char *name, int32 from, int32 to)
{
	v1_volume = to;
	calc_gains();
}

static void prefs_v2volume_changed(const char *name, int32 from, int32 to)
{
	v2_volume = to;
	calc_gains();
}

static void prefs_v3volume_changed(const char *name, int32 from, int32 to)
{
	v3_volume = to;
	calc_gains();
}

static void prefs_v4volume_changed(const char *name, int32 from, int32 to)
{
	v4_volume = to;
	calc_gains();
}

static void prefs_v1pan_changed(const char *name, int32 from, int32 to)
{
	v1_panning = to;
	calc_gains();
}

static void prefs_v2pan_changed(const char *name, int32 from, int32 to)
{
	v2_panning = to;
	calc_gains();
}

static void prefs_v3pan_changed(const char *name, int32 from, int32 to)
{
	v3_panning = to;
	calc_gains();
}

static void prefs_v4pan_changed(const char *name, int32 from, int32 to)
{
	v4_panning = to;
	calc_gains();
}

static void prefs_dualsep_changed(const char *name, int32 from, int32 to)
{
	dual_sep = to;
	calc_gains();
}

#ifdef SID_PLAYER
static void set_cycles_per_second(const char *to)
{
	if (strncmp(to, "6569", 4) == 0) {
		cycles_per_second = cycle_t(PAL_CLOCK);
#ifdef HAVE_CWSID
		ioctl(cwsid_fh, CWSID_IOCTL_PAL);
#endif
	} else if (strcmp(to, "6567R5") == 0) {
		cycles_per_second = cycle_t(NTSC_OLD_CLOCK);
#ifdef HAVE_CWSID
		ioctl(cwsid_fh, CWSID_IOCTL_NTSC);
#endif
	} else {
		cycles_per_second = cycle_t(NTSC_CLOCK);
#ifdef HAVE_CWSID
		ioctl(cwsid_fh, CWSID_IOCTL_NTSC);
#endif
	}
}

static void prefs_victype_changed(const char *name, const char *from, const char *to)
{
	set_cycles_per_second(to);
	SIDClockFreqChanged();
}

static void prefs_speed_changed(const char *name, int32 from, int32 to)
{
	speed_adjust = to;
}
#endif

void SIDInit()
{
	// Use Catweasel?
#ifdef HAVE_CWSID
	if (PrefsFindBool("cwsid")) {
		cwsid_fh = open(PrefsFindString("siddev"), O_WRONLY);
		if (cwsid_fh >= 0) {
			int i;
			if (ioctl(cwsid_fh, CWSID_IOCTL_CARDTYPE, &i) < 0 || i != CWSID_MAGIC) {
				close(cwsid_fh);
				cwsid_fh = -1;
			} else {
				ioctl(cwsid_fh, CWSID_IOCTL_RESET);
				ioctl(cwsid_fh, CWSID_IOCTL_DOUBLEBUFFER, 0);
			}
		}
	}
	if (cwsid_fh < 0)
		PrefsReplaceBool("cwsid", false);
#else
	PrefsReplaceBool("cwsid", false);
	cwsid_fh = -1;
#endif

	// Read preferences ("obtained" is set to have valid values in it if SDL_OpenAudio() fails)
	emulate_8580 = (strncmp(PrefsFindString("sidtype"), "8580", 4) == 0);
	set_sid_data();
	desired.freq = obtained.freq = PrefsFindInt32("samplerate");
	desired.format = obtained.format = PrefsFindBool("audio16bit") ? AUDIO_S16SYS : AUDIO_U8;
	desired.channels = obtained.channels = PrefsFindBool("stereo") ? 2 : 1;
	enable_filters = PrefsFindBool("filters");
	dual_sid = PrefsFindBool("dualsid");
	PrefsSetCallback("sidtype", prefs_sidtype_changed);
	PrefsSetCallback("samplerate", prefs_samplerate_changed);
	PrefsSetCallback("audio16bit", prefs_audio16bit_changed);
	PrefsSetCallback("stereo", prefs_stereo_changed);
	PrefsSetCallback("filters", prefs_filters_changed);
	PrefsSetCallback("dualsid", prefs_dualsid_changed);

#ifdef SID_PLAYER
	set_cycles_per_second(PrefsFindString("victype"));
	speed_adjust = PrefsFindInt32("speed");
	PrefsSetCallback("victype", prefs_victype_changed);
	PrefsSetCallback("speed", prefs_speed_changed);
#endif

	audio_effect = PrefsFindInt32("audioeffect");
	rev_feedback = PrefsFindInt32("revfeedback");
	PrefsSetCallback("audioeffect", prefs_audioeffect_changed);
	PrefsSetCallback("revdelay", prefs_revdelay_changed);
	PrefsSetCallback("revfeedback", prefs_revfeedback_changed);

	master_volume = PrefsFindInt32("volume");
	v1_volume = PrefsFindInt32("v1volume");
	v2_volume = PrefsFindInt32("v2volume");
	v3_volume = PrefsFindInt32("v3volume");
	v4_volume = PrefsFindInt32("v4volume");
	v1_panning = PrefsFindInt32("v1pan");
	v2_panning = PrefsFindInt32("v2pan");
	v3_panning = PrefsFindInt32("v3pan");
	v4_panning = PrefsFindInt32("v4pan");
	dual_sep = PrefsFindInt32("dualsep");
	PrefsSetCallback("volume", prefs_volume_changed);
	PrefsSetCallback("v1volume", prefs_v1volume_changed);
	PrefsSetCallback("v2volume", prefs_v2volume_changed);
	PrefsSetCallback("v3volume", prefs_v3volume_changed);
	PrefsSetCallback("v4volume", prefs_v4volume_changed);
	PrefsSetCallback("v1pan", prefs_v1pan_changed);
	PrefsSetCallback("v2pan", prefs_v2pan_changed);
	PrefsSetCallback("v3pan", prefs_v3pan_changed);
	PrefsSetCallback("v4pan", prefs_v4pan_changed);
	PrefsSetCallback("dualsep", prefs_dualsep_changed);
	calc_gains();

	// Set sample buffer size
	set_desired_samples(desired.freq);

	// Open audio device
	desired.callback = calc_buffer;
	desired.userdata = NULL;
#ifdef SID_PLAYER
	if (PrefsFindString("outfile") == NULL && cwsid_fh < 0) {
		if (SDL_OpenAudio(&desired, &obtained) < 0) {
			fprintf(stderr, "Couldn't initialize audio (%s)\n", SDL_GetError());
			exit(1);
		}
	}
#else
	if (cwsid_fh < 0)
		SDL_OpenAudio(&desired, &obtained);
#endif

	// Convert reverb delay to sample frame count
	set_rev_delay(PrefsFindInt32("revdelay"));

	// Compute number of cycles per sample frame and envelope table
	SIDClockFreqChanged();

	// Compute triangle table
	for (int i=0; i<0x1000; i++) {
		tri_table[i] = (i << 4) | (i >> 8);
		tri_table[0x1fff-i] = (i << 4) | (i >> 8);
	}

	// Compute filter tables
	for (int i=0; i<256; i++) {
#ifdef USE_FIXPOINT_MATHS
		ffreq_lp[i] = int32(CALC_RESONANCE_LP(i) * 65536.0);
		ffreq_hp[i] = int32(CALC_RESONANCE_HP(i) * 65536.0);
#else
		ffreq_lp[i] = CALC_RESONANCE_LP(i);
		ffreq_hp[i] = CALC_RESONANCE_HP(i);
#endif
	}

#ifdef SID_PLAYER
	// Compute galway noise table
	for (int i=0; i<16; i++)
		for (int j=0; j<64; j++)
			galway_tab[i * 64 + j] = sample_tab[(i * j) & 0x0f];
#else
	// Register snapshot chunk
	SnapshotRegisterChunk('S', 'I', 'D', ' ', sid1_chunk_read, sid1_chunk_write);
	SnapshotRegisterChunk('S', 'I', 'D', '2', sid2_chunk_read, sid2_chunk_write);

	// Start sound output
	SDL_PauseAudio(false);
#endif
}


/*
 *  Exit SID emulation
 */

void SIDExit()
{
#ifdef HAVE_CWSID
	if (cwsid_fh >= 0) {
		ioctl(cwsid_fh, CWSID_IOCTL_RESET);
		close(cwsid_fh);
		cwsid_fh = -1;
	}
#endif
	SDL_CloseAudio();
}


/*
 *  Reset SID emulation
 */

void sid_t::reset()
{
	memset(regs, 0, sizeof(regs));
	last_written_byte = 0;

#ifdef SID_PLAYER
	volume = 15;
	regs[24] = 0x0f;
#else
	volume = 0;
#endif

#ifdef HAVE_CWSID
	if (cwsid_fh >= 0 && sid_num == 0) {
		ioctl(cwsid_fh, CWSID_IOCTL_RESET);
		lseek(cwsid_fh, 24, SEEK_SET);
		::write(cwsid_fh, regs + 24, 1);
	}
#endif

	for (int v=0; v<3; v++) {
		voice[v].wave = WAVE_NONE;
		voice[v].eg_state = EG_IDLE;
		voice[v].count = voice[v].add = 0;
		voice[v].freq = voice[v].pw = 0;
		voice[v].eg_level = voice[v].s_level = 0;
		voice[v].a_add = voice[v].d_sub = voice[v].r_sub = eg_table[0];
		voice[v].gate = voice[v].ring = voice[v].test = false;
		voice[v].filter = voice[v].sync = voice[v].mute = false;
	}

	f_type = FILT_NONE;
	f_freq = f_res = 0;
	f_ampl = F_ONE;
	d1 = d2 = g1 = g2 = F_ZERO;
	xn1_l = xn2_l = yn1_l = yn2_l = F_ZERO;
	xn1_r = xn2_r = yn1_r = yn2_r = F_ZERO;

#ifdef SID_PLAYER
	v4_state = V4_OFF;
	v4_count = v4_add = 0;

	gn_adr = gn_tone_length = 0;
	gn_volume_add = 0;
	gn_tone_counter = 0;
	gn_base_cycles = gn_loop_cycles = 0;
	gn_last_count = 0;

	sm_adr = sm_end_adr = sm_rep_adr = 0;
	sm_volume = 0;
	sm_rep_count = 0;
	sm_big_endian = false;
#endif
}

void SIDReset(cycle_t now)
{
	SDL_LockAudio();
	sid1.reset();
	sid2.reset();
#ifdef SID_PLAYER
	memset(work_buffer, 0, sizeof(work_buffer));
#endif
	SDL_UnlockAudio();
}


/*
 *  Clock overflow
 */

void SIDClockOverflow(cycle_t sub)
{
}


/*
 *  Clock frequency changed (result of VIC type change)
 */

void SIDClockFreqChanged()
{
	// Compute number of cycles per sample frame
	sid_cycles = cycles_per_second / obtained.freq;
#ifdef USE_FIXPOINT_MATHS
	sid_cycles_frac = int32(float(cycles_per_second) * 65536.0 / obtained.freq);
#else
	sid_cycles_frac = float(cycles_per_second) / obtained.freq;
#endif

	// Compute envelope table
	static const uint32 div[16] = {
		9, 32, 63, 95, 149, 220, 267, 313,
		392, 977, 1954, 3126, 3906, 11720, 19531, 31251
	};
	for (int i=0; i<16; i++)
		eg_table[i] = (sid_cycles << 16) / div[i];

	// Recompute voice_t::add values
	sid1.write(0, sid1.regs[0], 0, false);
	sid1.write(7, sid1.regs[7], 0, false);
	sid1.write(14, sid1.regs[14], 0, false);
	sid2.write(0, sid2.regs[0], 0, false);
	sid2.write(7, sid2.regs[7], 0, false);
	sid2.write(14, sid2.regs[14], 0, false);
}


#ifdef SID_PLAYER
/*
 *  Set replay frequency
 */

void SIDSetReplayFreq(int freq)
{
	cia_timer = cycles_per_second / freq - 1;
}

/*
 *  Set speed adjustment
 */

void SIDAdjustSpeed(int percent)
{
	PrefsReplaceInt32("speed", percent);
}

/*
 *  Write to CIA timer A (changes replay frequency)
 */

void cia_tl_write(uint8 byte)
{
	cia_timer = (cia_timer & 0xff00) | byte;
}

void cia_th_write(uint8 byte)
{
	cia_timer = (cia_timer & 0x00ff) | (byte << 8);
}
#endif


/*
 *  Fill audio buffer with SID sound
 */

static void calc_sid(sid_t &sid, int32 &sum_output_left, int32 &sum_output_right)
{
	// Sampled voice (!! todo: gain/panning)
#if 0	//!!
	uint8 master_volume = sid.sample_buf[(sample_count >> 16) % SAMPLE_BUF_SIZE];
	sample_count += ((0x138 * 50) << 16) / obtained.freq;
#else
	uint8 master_volume = sid.volume;
#endif
#ifndef SID_PLAYER
	sum_output_left += sample_tab[master_volume] << 8;
	sum_output_right += sample_tab[master_volume] << 8;
#endif

	int32 sum_output_filter_left = 0, sum_output_filter_right = 0;

	// Loop for all three voices
	for (int j=0; j<3; j++) {
		voice_t *v = sid.voice + j;

		// Envelope generator
		uint16 envelope;

		switch (v->eg_state) {
			case EG_ATTACK:
				v->eg_level += v->a_add;
				if (v->eg_level > 0xffffff) {
					v->eg_level = 0xffffff;
					v->eg_state = EG_DECAY;
				}
				break;
			case EG_DECAY:
				if (v->eg_level <= v->s_level || v->eg_level > 0xffffff)
					v->eg_level = v->s_level;
				else {
					v->eg_level -= v->d_sub >> eg_dr_shift[v->eg_level >> 16];
					if (v->eg_level <= v->s_level || v->eg_level > 0xffffff)
						v->eg_level = v->s_level;
				}
				break;
			case EG_RELEASE:
				v->eg_level -= v->r_sub >> eg_dr_shift[v->eg_level >> 16];
				if (v->eg_level > 0xffffff) {
					v->eg_level = 0;
					v->eg_state = EG_IDLE;
				}
				break;
			case EG_IDLE:
				v->eg_level = 0;
				break;
		}
		envelope = (v->eg_level * master_volume) >> 20;

		// Waveform generator
		uint16 output;

		if (!v->test)
			v->count += v->add;

		if (v->sync && (v->count >= 0x1000000))
			v->mod_to->count = 0;

		v->count &= 0xffffff;

		switch (v->wave) {
			case WAVE_TRI:
				if (v->ring)
					output = tri_table[(v->count ^ (v->mod_by->count & 0x800000)) >> 11];
				else
					output = tri_table[v->count >> 11];
				break;
			case WAVE_SAW:
				output = v->count >> 8;
				break;
			case WAVE_RECT:
				if (v->count > (uint32)(v->pw << 12))
					output = 0xffff;
				else
					output = 0;
				break;
			case WAVE_TRISAW:
				output = tri_saw_table[v->count >> 16];
				break;
			case WAVE_TRIRECT:
				if (v->count > (uint32)(v->pw << 12))
					output = tri_rect_table[v->count >> 16];
				else
					output = 0;
				break;
			case WAVE_SAWRECT:
				if (v->count > (uint32)(v->pw << 12))
					output = saw_rect_table[v->count >> 16];
				else
					output = 0;
				break;
			case WAVE_TRISAWRECT:
				if (v->count > (uint32)(v->pw << 12))
					output = tri_saw_rect_table[v->count >> 16];
				else
					output = 0;
				break;
			case WAVE_NOISE:
				if (v->count >= 0x100000) {
					output = v->noise = noise_rand() << 8;
					v->count &= 0xfffff;
				} else
					output = v->noise;
				break;
			default:
				output = 0x8000;
				break;
		}
		int32 x = (int16)(output ^ 0x8000) * envelope;
		if (v->filter) {
			sum_output_filter_left += (x * v->left_gain) >> 4;
			sum_output_filter_right += (x * v->right_gain) >> 4;
		} else if (!(v->mute)) {
			sum_output_left += (x * v->left_gain) >> 4;
			sum_output_right += (x * v->right_gain) >> 4;
		}
	}

#ifdef SID_PLAYER
	// Galway noise/samples
	int32 v4_output = 0;
	switch (sid.v4_state) {

		case V4_GALWAY_NOISE:
			v4_output = galway_tab[(sid.gn_volume_add << 6) + (((sid.gn_last_count + sid.v4_count) >> 16) & 0x3f)] << 8;
			sid.v4_count += sid.v4_add;
			if ((sid.v4_count >> 16) >= sid.gn_tone_length) {
				if (sid.gn_tone_counter) {
					sid.gn_tone_counter--;
					sid.gn_last_count = sid.v4_count & 0xffff0000;
					sid.v4_count &= 0xffff;
					int div = ram[sid.gn_adr + sid.gn_tone_counter] * sid.gn_loop_cycles + sid.gn_base_cycles;
					if (div == 0)
						sid.v4_add = 0;
					else
						sid.v4_add = sid_cycles * 0x10000 / div;
				} else
					sid.v4_state = V4_OFF;
			}
			break;

		case V4_SAMPLE: {
			uint8 sample = ram[sid.sm_adr >> 1];
			if (sid.sm_big_endian)
				if (sid.sm_adr & 1)
					sample = sample & 0xf;
				else
					sample = sample >> 4;
			else
				if (sid.sm_adr & 1)
					sample = sample >> 4;
				else
					sample = sample & 0xf;
			v4_output = sample_tab[sid.sm_volume * 16 + sample] << 8;
			sid.v4_count += sid.v4_add;
			sid.sm_adr += sid.v4_count >> 16;
			sid.v4_count &= 0xffff;
			if (sid.sm_adr >= sid.sm_end_adr) {
				if (sid.sm_rep_count) {
					if (sid.sm_rep_count != 0xff)
						sid.sm_rep_count--;
					sid.sm_adr = sid.sm_rep_adr;
				} else
					sid.v4_state = V4_OFF;
			}
			break;
		}
	}
	sum_output_left += (v4_output * sid.v4_left_gain) >> 4;
	sum_output_right += (v4_output * sid.v4_right_gain) >> 4;
#endif

	// Filter
	if (enable_filters) {
#ifdef USE_FIXPOINT_MATHS	//!!
		int32 xn = sid.f_ampl.imul(sum_output_filter);
		int32 yn = xn+sid.d1.imul(xn1)+sid.d2.imul(xn2)-sid.g1.imul(yn1)-sid.g2.imul(yn2);
		sum_output_filter = yn;
#endif
		float xn = float(sum_output_filter_left) * sid.f_ampl;
		float yn = xn + sid.d1 * sid.xn1_l + sid.d2 * sid.xn2_l - sid.g1 * sid.yn1_l - sid.g2 * sid.yn2_l;
		sum_output_filter_left = int32(yn);
		sid.yn2_l = sid.yn1_l; sid.yn1_l = yn; sid.xn2_l = sid.xn1_l; sid.xn1_l = xn;
		xn = float(sum_output_filter_right) * sid.f_ampl;
		yn = xn + sid.d1 * sid.xn1_r + sid.d2 * sid.xn2_r - sid.g1 * sid.yn1_r - sid.g2 * sid.yn2_r;
		sum_output_filter_right = int32(yn);
		sid.yn2_r = sid.yn1_r; sid.yn1_r = yn; sid.xn2_r = sid.xn1_r; sid.xn1_r = xn;
	}

	// Add filtered and non-filtered output
	sum_output_left += sum_output_filter_left;
	sum_output_right += sum_output_filter_right;
}

static void calc_buffer(void *userdata, uint8 *buf, int count)
{
	uint16 *buf16 = (uint16 *)buf;
#ifdef SID_PLAYER
	int replay_limit = (obtained.freq * 100) / (cycles_per_second / (cia_timer + 1) * speed_adjust);
#endif

#ifndef SID_PLAYER
	// Index in sample_buf for reading, 16.16 fixed
#if 0	//!!
	uint32 sample_count = (sample_in_ptr + SAMPLE_BUF_SIZE/2) << 16;
#endif
#endif

	// Convert buffer length (in bytes) to frame count
	bool is_stereo = (obtained.channels == 2);
	bool is_16_bit = !(obtained.format == AUDIO_U8 || obtained.format == AUDIO_S8);
	if (is_stereo)
		count >>= 1;
	if (is_16_bit)
		count >>= 1;

	// Main calculation loop
	while (count--) {
		int32 sum_output_left = 0, sum_output_right = 0;

#ifdef SID_PLAYER
		// Execute 6510 play routine if due
		if (++replay_count >= replay_limit) {
			replay_count = 0;
			UpdatePlayAdr();
			CPUExecute(play_adr, 0, 0, 0, 1000000);
		}
#endif

#ifdef HAVE_CWSID
		if (cwsid_fh >= 0)
			continue;
#endif

		// Calculate output of voices from both SIDs
		calc_sid(sid1, sum_output_left, sum_output_right);
		if (dual_sid)
			calc_sid(sid2, sum_output_left, sum_output_right);

		// Apply audio effects (post-processing)
		if (audio_effect) {
			sum_output_left >>= 11;
			sum_output_right >>= 11;
			if (audio_effect == 1) {	// Reverb
				sum_output_left += (rev_feedback * work_buffer[wb_read_offset++]) >> 8;
				work_buffer[wb_write_offset++] = sum_output_left;
				sum_output_right += (rev_feedback * work_buffer[wb_read_offset]) >> 8;
				work_buffer[wb_write_offset] = sum_output_right;
			} else {					// Spatial
				sum_output_left += (rev_feedback * work_buffer[wb_read_offset++]) >> 8;
				work_buffer[wb_write_offset++] = sum_output_left;
				sum_output_right -= (rev_feedback * work_buffer[wb_read_offset]) >> 8;
				work_buffer[wb_write_offset] = sum_output_right;
			}
			wb_read_offset = (wb_read_offset + 1) & (WORK_BUFFER_SIZE - 1);
			wb_write_offset = (wb_write_offset + 1) & (WORK_BUFFER_SIZE - 1);
		} else {
			sum_output_left >>= 10;
			sum_output_right >>= 10;
		}

		// Clip to 16 bits
		if (sum_output_left > 32767)
			sum_output_left = 32767;
		else if (sum_output_left < -32768)
			sum_output_left = -32768;
		if (sum_output_right > 32767)
			sum_output_right = 32767;
		else if (sum_output_right < -32768)
			sum_output_right = -32768;

		// Write to output buffer
		if (is_16_bit) {
			if (is_stereo) {
				*buf16++ = sum_output_left;
				*buf16++ = sum_output_right;
			} else
				*buf16++ = (sum_output_left + sum_output_right) / 2;
		} else {
			if (is_stereo) {
				*buf++ = (sum_output_left >> 8) ^ 0x80;
				*buf++ = (sum_output_right >> 8) ^ 0x80;
			} else
				*buf++ = ((sum_output_left + sum_output_right) >> 9) ^ 0x80;
		}
	}
}

#ifdef SID_PLAYER
void SIDCalcBuffer(uint8 *buf, int count)
{
	calc_buffer(NULL, buf, count);
}

uint64 replay_start_time = 0;	// Start time of last replay
int32 over_time = 0;			// Time the last replay was too long

void SIDExecute()
{
	// Delay to maintain proper replay frequency
	uint64 now = GetTicks_usec();
	if (replay_start_time == 0)
		replay_start_time = now;
	uint32 replay_time = now - replay_start_time;
	uint32 adj_nominal_replay_time = uint32((cia_timer + 1) * 100000000.0 / (cycles_per_second * speed_adjust));
	int32 delay = adj_nominal_replay_time - replay_time - over_time;
	over_time = -delay;
	if (over_time < 0)
		over_time = 0;
	if (delay > 0) {
		Delay_usec(delay);
		int32 actual_delay = GetTicks_usec() - now;
		if (actual_delay + 500 < delay)
			Delay_usec(1);
		actual_delay = GetTicks_usec() - now;
		over_time += actual_delay - delay;
		if (over_time < 0)
			over_time = 0;
	}
	replay_start_time = GetTicks_usec();

	// Execute 6510 play routine
	UpdatePlayAdr();
	CPUExecute(play_adr, 0, 0, 0, 1000000);
}
#endif


/*
 *  Calculate IIR filter coefficients
 */

void sid_t::calc_filter()
{
	// Filter off? Then reset all coefficients
	if (f_type == FILT_NONE) {
		f_ampl = F_ZERO;
		d1 = d2 = g1 = g2 = F_ZERO;
		return;
	}

	// Calculate resonance frequency
	filt_t fr;
	if (f_type == FILT_LP || f_type == FILT_LPBP)
		fr = ffreq_lp[f_freq];
	else
		fr = ffreq_hp[f_freq];

	// Limit to <1/2 sample frequency, avoid div by 0 in case FILT_NOTCH below
	filt_t arg = fr / float(obtained.freq >> 1);
	if (arg > 0.99)
		arg = 0.99;
	if (arg < 0.01)
		arg = 0.01;

	// Calculate poles (resonance frequency and resonance)
	// The (complex) poles are at
	//   zp_1/2 = (-g1 +/- sqrt(g1^2 - 4*g2)) / 2
	g2 = 0.55 + 1.2 * arg * arg - 1.2 * arg + float(f_res) * 0.0133333333;
	g1 = -2.0 * sqrt(g2) * cos(M_PI * arg);

	// Increase resonance if LP/HP combined with BP
	if (f_type == FILT_LPBP || f_type == FILT_HPBP)
		g2 += 0.1;

	// Stabilize filter
	if (fabs(g1) >= g2 + 1.0) {
		if (g1 > 0.0)
			g1 = g2 + 0.99;
		else
			g1 = -(g2 + 0.99);
	}

	// Calculate roots (filter characteristic) and input attenuation
	// The (complex) roots are at
	//   z0_1/2 = (-d1 +/- sqrt(d1^2 - 4*d2)) / 2
	switch (f_type) {

		case FILT_LPBP:
		case FILT_LP:		// Both roots at -1, H(1)=1
			d1 = 2.0; d2 = 1.0;
			f_ampl = 0.25 * (1.0 + g1 + g2);
			break;

		case FILT_HPBP:
		case FILT_HP:		// Both roots at 1, H(-1)=1
			d1 = -2.0; d2 = 1.0;
			f_ampl = 0.25 * (1.0 - g1 + g2);
			break;

		case FILT_BP: {		// Roots at +1 and -1, H_max=1
			d1 = 0.0; d2 = -1.0;
			float c = sqrt(g2*g2 + 2.0*g2 - g1*g1 + 1.0);
			f_ampl = 0.25 * (-2.0*g2*g2 - (4.0+2.0*c)*g2 - 2.0*c + (c+2.0)*g1*g1 - 2.0) / (-g2*g2 - (c+2.0)*g2 - c + g1*g1 - 1.0);
			break;
		}

		case FILT_NOTCH:	// Roots at exp(i*pi*arg) and exp(-i*pi*arg), H(1)=1 (arg>=0.5) or H(-1)=1 (arg<0.5)
			d1 = -2.0 * cos(M_PI * arg); d2 = 1.0;
			if (arg >= 0.5)
				f_ampl = 0.5 * (1.0 + g1 + g2) / (1.0 - cos(M_PI * arg));
			else
				f_ampl = 0.5 * (1.0 - g1 + g2) / (1.0 + cos(M_PI * arg));
			break;

		// The following is pure guesswork...
		case FILT_ALL:		// Roots at 2*exp(i*pi*arg) and 2*exp(-i*pi*arg), H(-1)=1 (arg>=0.5) or H(1)=1 (arg<0.5)
			d1 = -4.0 * cos(M_PI * arg); d2 = 4.0;
			if (arg >= 0.5)
				f_ampl = (1.0 - g1 + g2) / (5.0 + 4.0 * cos(M_PI * arg));
			else
				f_ampl = (1.0 + g1 + g2) / (5.0 - 4.0 * cos(M_PI * arg));
			break;

		default:
			break;
	}
}


/*
 *  Calculate gain values for all voices
 */

void sid_t::calc_gain_voice(int32 volume, int32 panning, uint16 &left_gain, uint16 &right_gain)
{
	int gain;
	if (panning < -0x100)
		panning = -0x100;
	if (panning > 0x100)
		panning = 0x100;
	gain = (volume * (-panning + 0x100) * master_volume) >> 20;
	if (gain > 0x200)
		gain = 0x200;
	if (gain < 0)
		gain = 0;
	left_gain = gain;
	gain = (volume * (panning + 0x100) * master_volume) >> 20;
	if (gain > 0x200)
		gain = 0x200;
	if (gain < 0)
		gain = 0;
	right_gain = gain;
}

void sid_t::calc_gains(bool is_left_sid, bool is_right_sid)
{
	int32 pan_offset = 0;
	if (is_left_sid)
		pan_offset = -dual_sep;
	else if (is_right_sid)
		pan_offset = dual_sep;
	calc_gain_voice(v1_volume, v1_panning + pan_offset, voice[0].left_gain, voice[0].right_gain);
	calc_gain_voice(v2_volume, v2_panning + pan_offset, voice[1].left_gain, voice[1].right_gain);
	calc_gain_voice(v3_volume, v3_panning + pan_offset, voice[2].left_gain, voice[2].right_gain);
	calc_gain_voice(v4_volume, v4_panning + pan_offset, v4_left_gain, v4_right_gain);
}


/*
 *  Read from SID register
 */

uint32 sid_t::read(uint32 adr, cycle_t now)
{
	D(bug("sid_read from %04x at cycle %d\n", adr, now));

	switch (adr) {
		case 0x19:	// A/D converters
		case 0x1a:
			last_written_byte = 0;
			return 0xff;
		case 0x1b:	// Voice 3 oscillator/EG readout
		case 0x1c:
			last_written_byte = 0;
			return f_rand();
		default: {	// Write-only register: return last value written to SID
			uint8 ret = last_written_byte;
			last_written_byte = 0;
			return ret;
		}
	}
}

uint32 sid_read(uint32 adr, cycle_t now)
{
#ifdef SID_PLAYER
	return sid1.read(adr & 0x7f, now);
#else
	if (dual_sid) {
		if (adr & 0x20)
			return sid2.read(adr & 0x1f, now);
		else
			return sid1.read(adr & 0x1f, now);
	} else
		return sid1.read(adr & 0x1f, now);
#endif
}


/*
 *  Write to SID register
 */

void sid_t::write(uint32 adr, uint32 byte, cycle_t now, bool rmw)
{
	D(bug("sid_write %02x to %04x at cycle %d\n", byte, adr, now));

#ifdef SID_PLAYER
	// Writing to standard SID mirrored registers
	if ((adr & 0x1f) < 0x1d)
		adr &= 0x1f;
#endif

	last_written_byte = regs[adr] = byte;
	int v = adr/7;	// Voice number

#ifdef HAVE_CWSID
	if (cwsid_fh >= 0 && sid_num == 0 && adr < 0x1a) {
		lseek(cwsid_fh, adr, SEEK_SET);
		::write(cwsid_fh, regs + adr, 1);
		lseek(cwsid_fh, adr, SEEK_SET);
		::write(cwsid_fh, regs + adr, 1);
	}
#endif

	switch (adr) {
		case 0:
		case 7:
		case 14:
			voice[v].freq = (voice[v].freq & 0xff00) | byte;
#ifdef USE_FIXPOINT_MATHS
//!!			voice[v].add = sid_cycles_frac.imul((int)voice[v].freq);
#else
			voice[v].add = (uint32)(float(voice[v].freq) * sid_cycles_frac);
#endif
			break;

		case 1:
		case 8:
		case 15:
			voice[v].freq = (voice[v].freq & 0xff) | (byte << 8);
#ifdef USE_FIXPOINT_MATHS
//!!			voice[v].add = sid_cycles_frac.imul((int)voice[v].freq);
#else
			voice[v].add = (uint32)(float(voice[v].freq) * sid_cycles_frac);
#endif
			break;

		case 2:
		case 9:
		case 16:
			voice[v].pw = (voice[v].pw & 0x0f00) | byte;
			break;

		case 3:
		case 10:
		case 17:
			voice[v].pw = (voice[v].pw & 0xff) | ((byte & 0xf) << 8);
			break;

		case 4:
		case 11:
		case 18:
			voice[v].wave = (byte >> 4) & 0xf;
			if ((byte & 1) != voice[v].gate)
				if (byte & 1)	// Gate turned on
					voice[v].eg_state = EG_ATTACK;
				else			// Gate turned off
					if (voice[v].eg_state != EG_IDLE)
						voice[v].eg_state = EG_RELEASE;
			voice[v].gate = byte & 1;
			voice[v].mod_by->sync = byte & 2;
			voice[v].ring = byte & 4;
			if ((voice[v].test = byte & 8))
				voice[v].count = 0;
			break;

		case 5:
		case 12:
		case 19:
			voice[v].a_add = eg_table[byte >> 4];
			voice[v].d_sub = eg_table[byte & 0xf];
			break;

		case 6:
		case 13:
		case 20:
			voice[v].s_level = (byte >> 4) * 0x111111;
			voice[v].r_sub = eg_table[byte & 0xf];
			break;

		case 22:
			if (byte != f_freq) {
				f_freq = byte;
				if (enable_filters)
					calc_filter();
			}
			break;

		case 23:
			voice[0].filter = byte & 1;
			voice[1].filter = byte & 2;
			voice[2].filter = byte & 4;
			if ((byte >> 4) != f_res) {
				f_res = byte >> 4;
				if (enable_filters)
					calc_filter();
			}
			break;

		case 24:
			volume = byte & 0xf;
			voice[2].mute = byte & 0x80;
			if (((byte >> 4) & 7) != f_type) {
				f_type = (byte >> 4) & 7;
				xn1_l = xn2_l = yn1_l = yn2_l = F_ZERO;
				xn1_r = xn2_r = yn1_r = yn2_r = F_ZERO;
				if (enable_filters)
					calc_filter();
			}
			break;

#ifdef SID_PLAYER
		case 29:
			if (byte) {
				if (byte < 0xfc) {			// Galway noise
					gn_adr = (regs[0x1f] << 8) | regs[0x1e];
					gn_tone_length = regs[0x3d];
					gn_volume_add = regs[0x3e] & 15;
					gn_tone_counter = byte;
					gn_base_cycles = regs[0x5d];
					gn_loop_cycles = regs[0x3f];
					gn_last_count = 0;
					v4_count = 0;
					int div = ram[gn_adr + gn_tone_counter] * gn_loop_cycles + gn_base_cycles;
					if (div == 0)
						v4_add = 0;
					else
						v4_add = sid_cycles * 0x10000 / div;
					v4_state = V4_GALWAY_NOISE;

				} else if (byte == 0xfd) {	// Sample off
					v4_state = V4_OFF;

				} else {					// Sample on
					sm_adr = ((regs[0x1f] << 8) | regs[0x1e]) << 1;
					sm_end_adr = ((regs[0x3e] << 8) | regs[0x3d]) << 1;
					sm_rep_adr = ((regs[0x7f] << 8) | regs[0x7e]) << 1;
					sm_rep_count = regs[0x3f];
					sm_big_endian = regs[0x7d];
					switch (byte) {
						case 0xfc:
							sm_volume = 2;
							break;
						case 0xfe:
							sm_volume = 1;
							break;
						case 0xff:
							sm_volume = 0;
							break;
					};
					int div = (regs[0x5e] << 8) | regs[0x5d];
					if (regs[0x5f])
						div >>= regs[0x5f];
					if (div == 0) {
						v4_state = V4_OFF;
					} else {
						v4_count = 0;
						v4_add = sid_cycles * 0x10000 / div;
						v4_state = V4_SAMPLE;
					}
				}
			}
			break;
#endif
	}
}

void sid_write(uint32 adr, uint32 byte, cycle_t now, bool rmw)
{
#ifdef SID_PLAYER
	SDL_LockAudio();
	sid1.write(adr & 0x7f, byte, now, rmw);
	SDL_UnlockAudio();
#else
	SDL_LockAudio();

	if (dual_sid) {
		if (adr & 0x20)
			sid2.write(adr & 0x1f, byte, now, rmw);
		else
			sid1.write(adr & 0x1f, byte, now, rmw);
	} else
		sid1.write(adr & 0x1f, byte, now, rmw);

	SDL_UnlockAudio();
#endif
}


/*
 *  Read/write snapshot chunk
 */

#ifndef SID_PLAYER
void sid_t::chunk_read(size_t size)
{
	SDL_LockAudio();

	for (int i=0; i<=24; i++)
		write(i, ChunkReadInt8(), 0, false);
	last_written_byte = ChunkReadInt8();
	voice[0].count = ChunkReadInt32();
	voice[1].count = ChunkReadInt32();
	voice[2].count = ChunkReadInt32();
	voice[0].noise = ChunkReadInt32();
	voice[1].noise = ChunkReadInt32();
	voice[2].noise = ChunkReadInt32();
	voice[0].eg_state = ChunkReadInt8();
	voice[1].eg_state = ChunkReadInt8();
	voice[2].eg_state = ChunkReadInt8();
	voice[0].eg_level = ChunkReadInt32();
	voice[1].eg_level = ChunkReadInt32();
	voice[2].eg_level = ChunkReadInt32();

	SDL_UnlockAudio();
}

static void sid1_chunk_read(size_t size)
{
	sid1.chunk_read(size);
}

static void sid2_chunk_read(size_t size)
{
	sid2.chunk_read(size);
}

void sid_t::chunk_write()
{
	SDL_LockAudio();

	for (int i=0; i<=24; i++)
		ChunkWriteInt8(regs[i]);
	ChunkWriteInt8(last_written_byte);
	ChunkWriteInt32(voice[0].count);
	ChunkWriteInt32(voice[1].count);
	ChunkWriteInt32(voice[2].count);
	ChunkWriteInt32(voice[0].noise);
	ChunkWriteInt32(voice[1].noise);
	ChunkWriteInt32(voice[2].noise);
	ChunkWriteInt8(voice[0].eg_state);
	ChunkWriteInt8(voice[1].eg_state);
	ChunkWriteInt8(voice[2].eg_state);
	ChunkWriteInt32(voice[0].eg_level);
	ChunkWriteInt32(voice[1].eg_level);
	ChunkWriteInt32(voice[2].eg_level);

	SDL_UnlockAudio();
}

static bool sid1_chunk_write()
{
	sid1.chunk_write();
	return true;
}

static bool sid2_chunk_write()
{
	if (!dual_sid)
		return false;
	sid2.chunk_write();
	return true;
}
#endif
