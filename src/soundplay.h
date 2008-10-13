
#include "pluginproto.h"

static void *getplugin(void **data,const char *name, const char *header, uint32 size, plugin_info *);
static void destroyplugin(void *,void*);
static BView* configure(BMessage *config);

static status_t open(void *,const char *name, const char *header, uint32 size, BMessage *config);
static int32	read(void *,char *buf, ulong count);
static status_t	info(void *_synth, file_info *info);

