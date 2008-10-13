/*
 *  soundplay.cpp - SIDPlayer SoundPlay plugin
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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <MediaDefs.h>
#include <Path.h>

#include "soundplay.h"

#include "prefs.h"
#include "main.h"
#include "psid.h"
#include "sid.h"
#include "prefs_window.h"

static const char MY_URL_HEADER[] = "sid://";
const int MY_URL_HEADER_LEN = 6;

/* ===========================================================
	Plugin-descriptor structure.
	This structure is used to gather information about a plugin
	and	instantiate and destroy specific instances of a plugin.
	Since SIDPlayer is single-context, we set the single context
	flag to make SoundPlay load additional instances of this plugin
	when needed.
*/
static plugin_descriptor plugin={
	PLUGIN_DESCRIPTOR_MAGIC,PLUGIN_DESCRIPTOR_VERSION,
	"sp-sidplay",1, PLUGIN_IS_DECODER|PLUGIN_IS_SINGLE_CONTEXT,

	"SIDPlayer Decoder",
	"By Christian Bauer.\n\n"
	"SoundPlay plugin by\nMarco Nelissen.",
	NULL, //about,
	configure,
	getplugin,
	destroyplugin
};

/* ===========================================================
	Plugin-operations structure.
	This is used to actually perform operations on a plugin.
	It is returned by the getplugin() function from the 
	plugin_descriptor structure above.
*/
static decoder_plugin_ops ops={
	PLUGIN_DECODER_MAGIC,PLUGIN_DECODER_VERSION,

	open,
	NULL,	// close
	info,
	read,
	NULL,	// play
	NULL,	// stop
	NULL,	// setvolume
	NULL,	// setpitchandspeed
	NULL,	// seek,
	NULL,	// position,
	NULL	// bufferamount
};


/* ===========================================================
	Return the list of plugins in this file.
*/
static plugin_descriptor *plugs[]={
	&plugin,
	0
};

plugin_descriptor **get_plugin_list(void)
{
	return plugs;
}

/* ===========================================================
	Return the list of filetypes supported by all the plugins
	in this file
*/
static supported_type supportedtypes[]=
{
	{"audio/x-psid",	"SIDPlayer file",	"SIDPlayer file",			"sid",	"psid"},
	{NULL,				NULL,				NULL,						NULL,	NULL}
};

supported_type *get_supported_types(void)
{
	return supportedtypes;
}


/* ===========================================================
	Implementation of the plugin_descriptor functions
*/

static BWindow *prefs_window=NULL;

static bool initialized=false;

class autoinit
{
	public:
		autoinit()
		{
		}
		~autoinit()
		{
			if(initialized)
			{
				ExitAll();
				initialized = false;
			}
		}
};
static autoinit autoinit;

static void *getplugin(void **data, const char *filename, const char *header, uint32 size, plugin_info *info)
{
	if(!initialized)
	{
		initialized = true;
		int argc = 0;
		char **argv = NULL;
		InitAll(argc, argv);
	}
	if (strncasecmp(filename, MY_URL_HEADER, MY_URL_HEADER_LEN) == 0) {
		// name starts with "sid://", extract file name
		char name[B_FILE_NAME_LENGTH];
		char *q = strrchr(filename, '?');
		if (q == NULL)
			return NULL;
		int name_len = q - (filename + MY_URL_HEADER_LEN);
		strncpy(name, filename + MY_URL_HEADER_LEN, name_len);
		name[name_len] = 0;
		if(!IsPSIDFile(name))
			return NULL;
	} else {
		if(!IsPSIDFile(filename))
			return NULL;
	}
	return &ops;
}

static void destroyplugin(void *ops,void *_sh)
{
}

static BView* configure(BMessage *config)
{
	if (prefs_window_open && prefs_window) {
		prefs_window->Activate(true);
		return NULL;
	} else {
		prefs_window = new PrefsWindow();
		return NULL;
	}
}

/* ===========================================================
	Implementation of the decoder_plugin_ops functions
*/

status_t open(void *,const char *filename, const char *header, uint32 size, BMessage *config)
{
	char name[B_FILE_NAME_LENGTH];
	bool subsong_given = false;
	int song;

	if (strncasecmp(filename, MY_URL_HEADER, MY_URL_HEADER_LEN) == 0) {

		// name starts with "sid://", extract file name
		char *q = strrchr(filename, '?');
		if (q == NULL)
			return B_ERROR;
		subsong_given = true;
		song = atoi(q + 1);
		int name_len = q - (filename + MY_URL_HEADER_LEN);
		strncpy(name, filename + MY_URL_HEADER_LEN, name_len);
		name[name_len] = 0;

	} else
		strcpy(name, filename);

	// Load PSID file
	if (!LoadPSIDFile(name))
		return B_ERROR;

	// Select subsong if subsong number given
	if (subsong_given)
		SelectSong(song);

	return B_OK;
}


static status_t info(void *_sid, file_info *info)
{
	strcpy(info->name,"");
	strcpy(info->typedesc,"SID");
	strcpy(info->mimetype,"audio/x-psid");
	info->samplerate=44100;
	info->bitrate=0;
	info->numchannels=2;
	info->granularity=4096;
	info->framecount=0x7fffffff;
	info->samplesize=2;
	if(B_HOST_IS_LENDIAN)
		info->byteorder=B_LITTLE_ENDIAN;
	else
		info->byteorder=B_BIG_ENDIAN;
	info->sampleformat=B_LINEAR_SAMPLES;
	info->flags=PLUGIN_FILELENGTH_UNKNOWN;
	
	return B_OK;
}


int32 read(void *,char *buf, ulong numframes)
{
	SIDCalcBuffer((uint8*)buf, numframes*4);
	return numframes;
}


