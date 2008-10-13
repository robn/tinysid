#ifndef CLAMP_PLUGIN_MSGS_H
#define CLAMP_PLUGIN_MSGS_H

#define CLAMP_MSG_PLAYINFO		900
	#define CLAMP_PLAYINFO_LABEL "PlayInfo"
#define CLAMP_MSG_CHANGED		901
	#define CLAMP_CHANGED_LABEL "FileName"	// String
	#define CLAMP_SONG_ID		"SongId"	// Int32
#define CLAMP_MSG_ADD			902		// Add a SongId to get the new entry inserted before that song
	#define CLAMP_ADD_LABEL CLAMP_CHANGED_LABEL
#define CLAMP_MSG_DEL			903		// FileName or SongId
	#define CLAMP_DEL_LABEL CLAMP_CHANGED_LABEL
#define CLAMP_MSG_DEACTIVATE	904
	#define CLAMP_OBJECT_LABEL "Object"
#define CLAMP_GET_SETTINGS_FOLDER	905
	#define CLAMP_FOLDER_LABEL "Folder"

#endif
