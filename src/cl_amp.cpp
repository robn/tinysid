/*
 *  cl_amp.cpp - SIDPlayer CL-Amp plugin
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

#include "sys.h"

#include <AppKit.h>
#include <support/UTF8.h>
#include <stdio.h>

#include "prefs.h"
#include "main.h"
#include "psid.h"
#include "sid.h"
#include "prefs_window.h"

#include "InputPlugin.h"

class SIDPlugin;


// Size of audio buffer
const int AUDIO_BUFFER_SIZE = 4096;

// URL header for subsong recognition
static const char MY_URL_HEADER[] = "sid://";
const int MY_URL_HEADER_LEN = 6;


// Plugin class definition
class SIDPlugin : public InputPlugin {
public:
	SIDPlugin();
	virtual ~SIDPlugin();

	void Init();
	void Cleanup();

	// Thread A
	bool About(bool Question);
	bool Prefs(bool Question);
	bool Edit(const char *FileName, bool Question);
	bool GetMimeType(BMimeType *m, int nr);
	bool IsOur(const char *FileName);
	bool GetSongInfo(const char *FName, PlayerInfoStruct *Info);
	void AbortPlaying();
	void Pause(bool On);
	void NewSpeed(long Promille);
	void NewVolume(long Promille);

	// Thread B
	bool InitPlaying(const char *FileName, PlayerInfoStruct *Info);
	int GetAudio(char **Buff, int Size);
	void JumpTo(long NewTime);
	void CleanupPlaying();

private:
	uint8 buffer[AUDIO_BUFFER_SIZE];	// Audio data buffer
	PrefsWindow *prefs_window;			// Pointer to prefs window
};


/*
 *  CL-Amp plugin interface
 */

extern "C" _EXPORT InputPlugin *NewPlugin(int *version);

InputPlugin *NewPlugin(int *version)
{
	*version = CURRENT_INPUT_PLUGIN_VERSION;
	return new SIDPlugin();
}


/*
 *  Constructor
 */

SIDPlugin::SIDPlugin() : InputPlugin("PSID", "SIDPlayer C64 SID tune player by Christian Bauer")
{
	prefs_window = NULL;
}


/*
 *  Destructor
 */

SIDPlugin::~SIDPlugin()
{
}


/*
 *  Init/cleanup
 */

void SIDPlugin::Init()
{
	int argc = 0;
	char **argv = NULL;
	InitAll(argc, argv);
}

void SIDPlugin::Cleanup()
{
	if (prefs_window_open && prefs_window) {
		prefs_window->PostMessage(B_QUIT_REQUESTED);
		while (prefs_window_open)
			snooze(1000);
	}
	ExitAll();
}


/*
 *  About window
 */

bool SIDPlugin::About(bool Question)
{
	if (Question)
		return true;

	AboutWindow();
	return true;
}


/*
 *  Prefs window
 */

bool SIDPlugin::Prefs(bool Question)
{
	if (Question)
		return true;

	if (prefs_window_open && prefs_window) {
		prefs_window->Activate(true);
		return false;
	} else {
		prefs_window = new PrefsWindow();
		return true;
	}
}


/*
 *  Edit window
 */

bool SIDPlugin::Edit(const char *FileName, bool Question)
{
	return false;
}


/*
 *  Get MIME type for supported audio files
 */

bool SIDPlugin::GetMimeType(BMimeType *m, int nr)
{
	if (nr == 0) {
		m->SetTo("audio/x-psid");
		m->SetLongDescription("C64 SID tune");
		return true;
	} else
		return false;
}


/*
 *  Check if file is handled by this plugin
 */

bool SIDPlugin::IsOur(const char *FileName)
{
	if (strncasecmp(FileName, MY_URL_HEADER, MY_URL_HEADER_LEN) == 0) {

		// name starts with "sid://", extract file name
		char name[B_FILE_NAME_LENGTH];
		char *q = strrchr(FileName, '?');
		if (q == NULL)
			return false;
		int name_len = q - (FileName + MY_URL_HEADER_LEN);
		strncpy(name, FileName + MY_URL_HEADER_LEN, name_len);
		name[name_len] = 0;
		return IsPSIDFile(name);

	} else
		return IsPSIDFile(FileName);
}


/*
 *  Get information about audio file
 */

static bool get_song_info(const char *file_name, PlayerInfoStruct *Info, int &number_of_songs, int &song, bool default_song = true)
{
	// Load header
	uint8 header[PSID_MAX_HEADER_LENGTH];
	if (!LoadPSIDHeader(file_name, header))
		return false;

	// Get number of subsongs and default song
	number_of_songs = read_psid_16(header, PSID_NUMBER);
	if (number_of_songs == 0)
		number_of_songs = 1;
	if (default_song) {
		song = read_psid_16(header, PSID_DEFSONG);
		if (song)
			song--;
	}
	if (song >= number_of_songs)
		song = 0;

	// Set info
	char psid_name[64];
	int32 sl = 32, dl = 64, state = 0;
	convert_to_utf8(B_ISO1_CONVERSION, (char *)(header + PSID_NAME), &sl, psid_name, &dl, &state);
	if (number_of_songs > 1) {
		sprintf(Info->Title, "%s (%d/%d)", psid_name, song + 1, number_of_songs);
	} else
		strcpy(Info->Title, psid_name);
	Info->Flags = INPLUG_NO_TOTTIME | INPLUG_NO_CURRTIME | INPLUG_HANDLE_SPEED;
	Info->Frequency = 44100;
	Info->Stereo = true;
	return true;
}

bool SIDPlugin::GetSongInfo(const char *FileName, PlayerInfoStruct *Info)
{
	char name[B_FILE_NAME_LENGTH + 16];
	if (strncasecmp(FileName, MY_URL_HEADER, MY_URL_HEADER_LEN) == 0) {

		// name starts with "sid://", extract file name
		char *q = strrchr(FileName, '?');
		if (q == NULL)
			return false;
		int song = atoi(q + 1);
		int name_len = q - (FileName + MY_URL_HEADER_LEN);
		strncpy(name, FileName + MY_URL_HEADER_LEN, name_len);
		name[name_len] = 0;

		// Get info
		int number_of_songs;
		return get_song_info(name, Info, number_of_songs, song, false);

	} else {

		// Ordinary file name, get info and number of subsongs
		int number_of_songs, default_song;
		if (!get_song_info(FileName, Info, number_of_songs, default_song))
			return false;

		// Add subsongs other than default song to playlist in the correct order
		for (int i=0; i<number_of_songs; i++) {
			if (i != default_song) {
				sprintf(name, "%s%s?%d", MY_URL_HEADER, FileName, i);
				if (i < default_song)
					SendToCLAmp_AddFile(name, Info->SongId);
				else
					SendToCLAmp_AddFile(name);
			}
		}
	}
	return true;
}


/*
 *  Special handling for aborting playing
 */

void SIDPlugin::AbortPlaying()
{
}


/*
 *  Special handling for pause
 */

void SIDPlugin::Pause(bool On)
{
}


/*
 *  Adjust playback speed
 */

void SIDPlugin::NewSpeed(long Promille)
{
	SIDAdjustSpeed(Promille / 10);
}


/*
 *  Adjust playback volume
 */

void SIDPlugin::NewVolume(long Promille)
{
}


/*
 *  Prepare for playback
 */

bool SIDPlugin::InitPlaying(const char *FileName, PlayerInfoStruct *Info)
{
	char name[B_FILE_NAME_LENGTH];
	bool subsong_given = false;
	int song;

	if (strncasecmp(FileName, MY_URL_HEADER, MY_URL_HEADER_LEN) == 0) {

		// name starts with "sid://", extract file name
		char *q = strrchr(FileName, '?');
		if (q == NULL)
			return false;
		subsong_given = true;
		song = atoi(q + 1);
		int name_len = q - (FileName + MY_URL_HEADER_LEN);
		strncpy(name, FileName + MY_URL_HEADER_LEN, name_len);
		name[name_len] = 0;

	} else
		strcpy(name, FileName);

	// Load PSID file
	if (!LoadPSIDFile(name))
		return false;

	// Select subsong if subsong number given
	if (subsong_given)
		SelectSong(song);

	// Set data
	Info->Flags = INPLUG_NO_TOTTIME | INPLUG_NO_CURRTIME | INPLUG_HANDLE_SPEED;
	Info->Frequency = 44100;
	Info->Stereo = true;
	return true;
}


/*
 *  Audio stream callback function
 */

int SIDPlugin::GetAudio(char **Buff, int Size)
{
	SIDCalcBuffer(buffer, AUDIO_BUFFER_SIZE);
	*Buff = (char *)buffer;
	return AUDIO_BUFFER_SIZE;
}


/*
 *  Set playback position
 */

void SIDPlugin::JumpTo(long NewTime)
{
}


/*
 *  Stop playback
 */

void SIDPlugin::CleanupPlaying()
{
}
