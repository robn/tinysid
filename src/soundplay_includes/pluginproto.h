/* ==================================================
       SoundPlay plugin specification.
 ================================================== */

#ifndef _PLUGIN_PROTO_H
#define _PLUGIN_PROTO_H

#include <SupportDefs.h>
#include "Playlist.h"

/*
	To let SoundPlay know which filetypes you support, you fill in an
	array of these. SoundPlay will add both an uppercase and a lowercase 
	version of the extensions you specify to the mime database.
*/
typedef struct supported_type
{
	char *mimetype;
	char *longdesc;
	char *shortdesc;
	char *extension1;
	char *extension2;
} supported_type;


/*
 Operations that can be performed before the plugin is really active.
 These functions globally affect and describe a plugin. They are
 used to identify and configure the plugin, and to instantiate one
 particular instance of the plugin.
 All types of plugins are handled through this interface.
*/

class SoundPlayController;

typedef struct plugin_info
{
	SoundPlayController *controller;
	entry_ref *ref;
} plugin_info;

typedef	void		op_about();
typedef	BView*		op_configure(BMessage *config);
typedef void*		op_instantiate(void **data, const char *name, const char *header, uint32 size, plugin_info *pluginfo);
typedef void		op_destroy(void *plugin_ops, void *data);

/* And this structure contains the above functions. */
typedef struct plugin_descriptor
{
	// first some bookkeeping stuff
	uint32				desc_magic;				// magic number
	uint32				desc_version;			// version of this plugin structure

	const char*			id; 					// a unique string identifying your plugin
	uint32				version;				// distinguish between different version of the same plugin
	uint32				flags;					// PLUGIN_IS_DECODER, PLUGIN_IS_FILTER, PLUGIN_IS_VISUAL

	const char*			name;					// MUST be filled in
	const char*			aboutstring;			// Simple about string, in case you don't want to implement the About() function.

	op_about			(*About);				// Leave NULL if not implemented
	op_configure		(*Configure);			// Leave NULL if not implemented

	op_instantiate		(*Instantiate_Plugin);	// MUST be implemented, instantiates all types of plugins, returns pointer to plugin struct (see below)
	op_destroy			(*Destroy_Plugin);		// MUST be implemented, destroys the plugin created by Instantiate_Plugin()
} plugin_descriptor;


/*
 Only these function need to be exported.
 They return a pointer to an array of pointers to plugin_descriptor,
 which lists all the plugins in this plugin-file, and a pointer to an
 array of supportedtypes.
*/
extern "C" plugin_descriptor _EXPORT **get_plugin_list(void);
extern "C" supported_type _EXPORT *get_supported_types(void);


/*
 These are the operations that can be performed on a particular instance
 of a decoder-plugin, once it has been instantiated.
 The void* that each function gets as the first argument is the data pointer
 that was filled in by Instantiate_Plugin
*/

struct file_info;

typedef	status_t	op_decoder_open(void*,const char *name, const char *header, uint32 size, BMessage *config);
typedef	void		op_decoder_close(void*);
typedef	status_t	op_decoder_info(void*,file_info*);
typedef	int32		op_decoder_read(void *,char *buf, ulong count);
typedef	status_t	op_decoder_play(void*);
typedef	status_t	op_decoder_stop(void*);
typedef	status_t	op_decoder_setvolume(void*,float);
typedef	status_t	op_decoder_setspeed(void *,float speed);
typedef	status_t	op_decoder_seek(void*, uint32 pos);
typedef	uint32		op_decoder_position(void*);
typedef float		op_decoder_bufferamount(void*);

/*
 The following structure contains pointers to the above
 functions. Simply leave NULL if not implemented.
*/

typedef struct decoder_plugin_ops
{
	// first some bookkeeping stuff
	uint32				ops_magic;					// magic number
	uint32				ops_version;				// version of this plugin structure

	// and the function pointers.
	op_decoder_open				(*Open);			// leave NULL if not implemented
	op_decoder_close			(*Close);			// leave NULL if not implemented
	op_decoder_info				(*Info);			// MUST be implemented

	op_decoder_read				(*Read);			// leave NULL for input filters that don't produce data

	op_decoder_play				(*Play);			// leave NULL if you do provide data
	op_decoder_stop				(*Stop);			// leave NULL if you do provide data
	op_decoder_setvolume		(*SetVolume);		// leave NULL if you do provide data
	op_decoder_setspeed			(*SetSpeed);		// leave NULL if you do provide data
	op_decoder_seek				(*Seek);			// leave NULL if seeking is not possible (streams)
	op_decoder_position			(*Position);		// leave NULL if you can't provide position-info
	op_decoder_bufferamount		(*BufferAmount);	// leave NULL if you don't want to display a buffer-indicator
} decoder_plugin_ops;


#define PLUGIN_STRING_LENGTH 256

typedef struct file_info
{
	char				name[PLUGIN_STRING_LENGTH];		// a nicer name for the file, zero-length if none
	char				typedesc[PLUGIN_STRING_LENGTH];	// a description of the file
	char				mimetype[PLUGIN_STRING_LENGTH];	// mimetype

	float				samplerate;
	float				bitrate;
	uint32				numchannels;
	uint32				granularity;			// in frames
	uint32				framecount;
	uint32				samplesize;
	int32				byteorder;
	int32				sampleformat;

	uint64				flags;					// various flags
} file_info;


/*
 These are the operations that can be performed on a particular instance
 of a filter-plugin, once it has been instantiated.
 The void* that each function gets as the first argument is the data pointer
 that was filled in by Instantiate_Plugin
*/

typedef void		op_filter_filechange(void*, const char *name, const char *path);
typedef	status_t	op_filter_filter(void*, short *buffer,int32 framecount, void *info);
typedef	status_t	op_filter_filter_float(void*, float **input, float **output, int32 framecount, void *info);
typedef	BView*		op_filter_configure(void*);
typedef void		op_filter_setconfig(void*,BMessage *config);
typedef void		op_filter_getconfig(void*,BMessage *config);

// The following structure contains pointers to the above
// functions. Simply leave NULL if not implemented.

typedef struct filter_plugin_ops
{
	// first some bookkeeping stuff
	uint32				ops_magic;						// magic number
	uint32				ops_version;					// version of this plugin structure

	// and the function pointers.
	op_filter_filechange		(*FileChange);			// leave NULL if not implemented
	op_filter_filter			(*Filter);				// filter a buffer of data, leave NULL if you implement FilterFloat
	op_filter_configure			(*Configure);			// leave NULL if no run-time config
	op_filter_setconfig			(*SetConfig);			// leave NULL if no run-time config
	op_filter_getconfig			(*GetConfig);			// leave NULL if no run-time config
	op_filter_filter_float		(*FilterFloat);			// filter floats, leave NULL if you implement Filter
} filter_plugin_ops;

// User Interface Plugin classes

enum {
	FILTER_HOTKEYS	= 1,
	FILTER_REFS		= 2
};

enum {
	CONTROLLER_ADD				= 'ct\0\1',
	CONTROLLER_REMOVE			= 'ct\0\2',
	CONTROLLER_PLAYLISTEDITOR	= 'ct\0\3'
};

class SoundPlayController
{
public:
	int32		Version();			// soundplay version, encoded like this: X.Y.Z -> 0x000XYZ00
	const char *VersionString();	// version as a string, e.g. "3.2"
	
	void		Quit(void);
	void		DisableInterface(const char *id);
	void		HideMainInterface(void);
	void		ShowMainInterface(void);
	bool		IsMainInterfaceHidden(void);

	// You must lock the controller object before doing anything
	// with its tracks, and unlock it afterwards
	// Note that as long as you have a PlaylistPtr for a playlist, 
	// that playlist will remain valid. Its associated controls
	// might go away (i.e. the playlist gets removed from the controller),
	// but you can still work with the files in the playlist, or add it
	// to the controller again.
	void		Lock(void);
	void		Unlock(void);
	uint32		CountPlaylists(void);
	PlaylistPtr	PlaylistAt(uint32 index);
	PlaylistPtr	AddPlaylist();						// create a new playlist+controls
	status_t	AddPlaylist(PlaylistPtr playlist);	// add an existing playlist to the controller
	status_t	RemovePlaylist(PlaylistPtr playlist);
	void		OpenEditor(PlaylistPtr playlist);

	void		AddWindow(BWindow*, int32 filterflags=FILTER_HOTKEYS|FILTER_REFS);		// tell SoundPlay about your window(s) so that SP can do some hotkey-management for it and accept dropped files
	status_t	AddListener(BHandler *handler);
	status_t	RemoveListener(BHandler *handler);
	
	// plugins can use these functions to store and retrieve preferences as BMessages
	status_t	StorePreference(const char *name, const BMessage *message);
	status_t	RetrievePreference(const char *name, BMessage *message);

private:
#ifdef CONTROLLER_SECRET_INNER_WORKINGS
CONTROLLER_SECRET_INNER_WORKINGS
#endif
};


typedef status_t	op_ui_show(void*);
typedef void		op_ui_hide(void*);
typedef void		op_ui_setconfig(void*,BMessage *config);
typedef void		op_ui_getconfig(void*,BMessage *config);

// send a message with this constant to SoundPlay to enable
// an interface. Specify the interface by adding a string 
// called "interface" containing the plugin-id to the message.
const uint32 ENABLE_INTERFACE= '!int';

typedef struct interface_plugin_ops
{
	// first some bookkeeping stuff
	uint32				ops_magic;				// magic number
	uint32				ops_version;			// version of this plugin structure

	op_ui_show			(*Show);
	op_ui_hide			(*Hide);
	op_ui_setconfig		(*SetConfig);			// leave NULL if not implemented
	op_ui_getconfig		(*GetConfig);			// leave NULL if not implemented
} interface_plugin_ops;



/*
Typical sequence of events for a decoder plugin:
	- get_plugin_list is called to get the list of plugins in the imagefile
	  (typically returns a single plugin_descriptor, but could return multiple)
	- plugin_descriptor::SupportedTypes is called to find the supported types and add them to the mime database
	- plugin_descriptor::Instantiate_Plugin is called to get a new decoder_plugin_ops
	if successful:
	- decoder_plugin_ops::Open is called to open the file
	- decoder_plugin_ops::Info is called to get information about the file
	- decoder_plugin_ops::Read is called to read data from the file   -OR-  decoder_plugin_ops::Play is called to start playback of a non data-producing file
	- decoder_plugin_ops::Seek/Position/etcetera get called multiple times during the playback
	- decoder_plugin_ops::Close is called to close the file
	- plugin_descriptor::Destroy_Plugin is called to free the plugin_ops structure
*/


/*
Typical sequence of events for a filter plugin:
	- get_plugin_list is called to get the list of plugins in the imagefile
	  (typically returns a single plugin_descriptor, but could return multiple)
	- plugin_descriptor::Instantiate_Plugin is called to get a new filter_plugin_ops
	- filter_plugin_ops::SetConfig is called to set the configuration
	- filter_plugin_ops::Filter is called a number of times to filter buffers of data
	- plugin_descriptor::Destroy_Plugin is called to free the filter_plugin_ops structure
*/


// These are the "magic values" used to recognize structures
enum plugin_magic {
	PLUGIN_DESCRIPTOR_MAGIC='desc',
	PLUGIN_DECODER_MAGIC='inpt',
	PLUGIN_FILTER_MAGIC='filt',
	PLUGIN_VISUAL_MAGIC='visu',
	PLUGIN_INTERFACE_MAGIC='face',
	PLUGIN_PLAYLIST_MAGIC='edit'
};

// The current version of the structures
enum plugin_version {
	PLUGIN_DESCRIPTOR_VERSION=3,
	PLUGIN_DECODER_VERSION=3,
	PLUGIN_FILTER_VERSION=4,
	PLUGIN_VISUAL_VERSION=3,
	PLUGIN_INTERFACE_VERSION=4,
	PLUGIN_PLAYLIST_VERSION=4
};


// flags for the plugin descriptor structures
enum plugin_type {
	PLUGIN_IS_DECODER=1,
	PLUGIN_IS_FILTER=2,
	PLUGIN_IS_VISUAL=4,
	PLUGIN_IS_INTERFACE=16,			// old interface (8) was dropped after 3.6
	PLUGIN_IS_SINGLE_CONTEXT=0x10000000
};

// The following are reported in file_info::flags
const uint64 PLUGIN_POSITION_IS_RELATIVE
		=0x0000000200000000LL;	// Seek() and Position() are not absolute positions, but
								// relative to whatever the plugin reported as framecount.
								// Setting this bit implies that playing backwards is not
																		// possible.
const uint64 PLUGIN_REQUIRES_CONTIGUOUS_PHYSICAL_MEMORY	
		=0x0000000100000000LL;	// plugin cannot load into memory consisting of 
								// multiple areas. Should only be needed by plugins that
								// do DMA. Doesn't work well with relative positioning
								// or granularity!=1

const uint64 PLUGIN_MINIMAL_BUFFERING
		=0x0000000400000000LL;  // do less read-ahead. Use this only for "realtime" data, e.g.
								// data captured live from an audio input

const uint64 PLUGIN_FILELENGTH_UNKNOWN
		=0x0000000000000001LL;	// same as CL-Amp's INPLUG_NO_TOTTIME
const uint64 PLUGIN_NO_ELAPSEDTIME
		=0x0000000000000002LL;	// same as CL-Amp's INPLUG_NO_CURRTIME

#endif

