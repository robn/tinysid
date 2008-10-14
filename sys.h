/*
 *  sys.h - System-dependant functions
 *
 *  SIDPlayer (C) Copyright 1996-2004 Christian Bauer
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

#ifndef SYS_H
#define SYS_H

#include "types.h"


#if defined (SDL)

#include <SDL.h>

#else

// Dummy SDL definitions for SIDPlayer
typedef struct {
    int freq;            // DSP frequency -- samples per second
    uint16 format;        // Audio data format
    uint8 channels;        // Number of channels: 1 mono, 2 stereo
    uint8 silence;        // Audio buffer silence value (calculated)
    uint16 samples;        // Audio buffer size in samples
    uint32 size;        // Audio buffer size in bytes (calculated)
    void (*callback)(void *userdata, uint8 *stream, int len);
    void  *userdata;
} SDL_AudioSpec;

#define AUDIO_U8 0x0008
#define AUDIO_S8 0x8008
#define AUDIO_S16SYS 0x0010

#define SDL_OpenAudio(desired, obtained) (0)
#define SDL_CloseAudio()
#define SDL_PauseAudio(onoff)
#define SDL_LockAudio()
#define SDL_UnlockAudio()
#define SDL_GetError() ("unknown error")

#endif

// Microsecond-resolution timing functions
extern uint64 GetTicks_usec();
extern void Delay_usec(uint32 usec);

#endif
