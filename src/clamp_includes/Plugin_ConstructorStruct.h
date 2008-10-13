#ifndef CLAMP_PLUGIN_CONSTRSTRUCT_H
#define CLAMP_PLUGIN_CONSTRSTRUCT_H

struct PluginConstructorStruct {
	int Version;	//	Filled in directly by the NewPlugin function
	const char *SettingsFolder;
};

#endif
