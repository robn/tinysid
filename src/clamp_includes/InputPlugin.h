
#ifndef CLAMP_INPUT_PLUGIN_H
#define CLAMP_INPUT_PLUGIN_H

#include <AppKit.h>
#include <StorageKit.h>

#include "Plugin_Messages.h"
#include "Plugin_ConstructorStruct.h"

#include "InputPlugin_InfoStruct.h"

#define MAX_GETAUDIO_SIZE (10*1024)

// The class description of the object that will be returned when CL-Amp ask the AddOn (Plugin) for its object...
class InputPlugin {
public:
// Used by CL-Amp. Don't bother about these functions!
					//	InputPlugin(char *label, char *desc, struct InputPluginFuncs *F) { CLAmpHandler=NULL; Label= label; Description= desc; Master= false; }
	InputPlugin(char *label, char *desc) { CLAmpHandler=NULL; Label= label; Description= desc; Master= false; }
	virtual ~InputPlugin() { }
	void SetHandler(BHandler *handler) { CLAmpHandler= handler; }
	void SetMaster() { Master= true; }

// =================================================================
// Here comes the functions you have to provide in your plugin code!

// No thread considerations with this function!
	virtual void Init()=0;
	virtual void Cleanup() {};
	// 2 special special functions follows, not needed for most plugins!!
	virtual void *GetUserData()	{ return(NULL); }
	virtual void  SetUserData(void *UserData) { }

// These functions will run in thread A, dont let functions from A use data from B or vice verse...
	virtual bool About(bool Question)=0;
	virtual bool Prefs(bool Question)=0;
	virtual bool Edit (const char *FileName, bool Question)=0;
	virtual bool GetMimeType(BMimeType *m, int Nr)=0;
	virtual bool IsOur(const char *FileName)=0;
	virtual bool GetSongInfo(const char *FileName, struct PlayerInfoStruct *Info)=0;
	virtual void AbortPlaying()=0;
	virtual void Pause(bool On)=0;
	virtual void NewSpeed  (long Promille)=0;
	virtual void NewVolume (long Promille)=0;
	// 2 special functions follows, not needed for most plugins!!
	virtual void NewPanning(long Promille) {}
	virtual bool CanCrossFade(char *FileName1, char *FileName2) { return(true); }

// These functions will run in thread B, dont let functions from B use data from A or vice verse...
	virtual bool InitPlaying(const char *FileName, struct PlayerInfoStruct *Info)=0;
	virtual int	 GetAudio(char **Buff, int Size)=0;
	virtual void JumpTo(long NewTime)=0;
	virtual void CleanupPlaying()=0;

// End of the functions you have to provide in your plugin code!
// =================================================================

// Help functions you can call to get data from the InputPlugin class itself
	const char *GetLabel() { return Label; }
	const char *GetDescription() { return Description; }
	BHandler *GetCLAmpHandler() { return CLAmpHandler; }
	void SendToCLAmp(struct PlayerInfoStruct *info) { BMessage Msg(CLAMP_MSG_PLAYINFO); BLooper *l; if (CLAmpHandler && (l=CLAmpHandler->Looper())) { Msg.AddData(CLAMP_PLAYINFO_LABEL, B_STRING_TYPE, (char *)info, sizeof(*info)); l->PostMessage (&Msg, CLAmpHandler); } }
	void SendToCLAmp_ChangedFile(const char *FileName) { BMessage Msg(CLAMP_MSG_CHANGED); BLooper *l; if (CLAmpHandler && (l=CLAmpHandler->Looper())) { Msg.AddString(CLAMP_CHANGED_LABEL, FileName); l->PostMessage (&Msg, CLAmpHandler); } }
	void SendToCLAmp_AddFile(const char *FileName) { BMessage Msg(CLAMP_MSG_ADD); BLooper *l; if (CLAmpHandler && (l=CLAmpHandler->Looper())) { Msg.AddString(CLAMP_ADD_LABEL, FileName); l->PostMessage (&Msg, CLAmpHandler); } }
	void SendToCLAmp_AddFile(const char *FileName, long SongId) { BMessage Msg(CLAMP_MSG_ADD); BLooper *l; if (CLAmpHandler && (l=CLAmpHandler->Looper())) { Msg.AddString(CLAMP_ADD_LABEL, FileName); Msg.AddInt32(CLAMP_SONG_ID, SongId); l->PostMessage (&Msg, CLAmpHandler); } }
	void SendToCLAmp_DelFile(const char *FileName) { BMessage Msg(CLAMP_MSG_DEL); BLooper *l; if (CLAmpHandler && (l=CLAmpHandler->Looper())) { Msg.AddString(CLAMP_DEL_LABEL, FileName); l->PostMessage (&Msg, CLAmpHandler); } }
	void SendToCLAmp_DelFile(long SongId) { BMessage Msg(CLAMP_MSG_DEL); BLooper *l; if (CLAmpHandler && (l=CLAmpHandler->Looper())) { Msg.AddInt32(CLAMP_SONG_ID, SongId); l->PostMessage (&Msg, CLAmpHandler); } }
	bool IsMaster() { return (Master); }
private:
	char *Label, *Description;
	BHandler *CLAmpHandler;
	bool Master;
};

#define CURRENT_INPUT_PLUGIN_VERSION  3
#define INPUT_PLUGIN_VERSION_MASK	0x0fff
#define PUT_THIS_PLUGIN_LAST 		0x1000

#endif
