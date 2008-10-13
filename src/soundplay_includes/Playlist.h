

#ifndef _PLAYLIST_H
#define _PLAYLIST_H

#include <List.h>
#include <Entry.h>
#include <SupportKit.h>
#include <Message.h>


// messages that you receive when watching a playlist
enum {
	PLAYLIST_ADD		= 'pl\0\1',	// something was added to the playlist
	PLAYLIST_REMOVE		= 'pl\0\2',	// something was removed from the playlist
	PLAYLIST_EMPTY		= 'pl\0\3',	// playlist was emptied
	PLAYLIST_DESTROYED	= 'pl\0\4',	// playlist no longer exists. Note that you can only 
									// receive this if a playlist goes away for which you
									// registered as a listener, but didn't keep a PlaylistPtr
									// for. Needless to say, this is a Bad Thing.
	PLAYLIST_LOST_CONTROLS		= 'pl\0\5',	// playlist lost its set of controls
	PLAYLIST_GAINED_CONTROLS	= 'pl\0\6',	// playlist gained a set of controls
	PLAYLIST_CURRENT			= 'pl\0\7',	// the "current item" changed
	PLAYLIST_SORTED				= 'pl\1\0', // the playlist just got sorted. You'll want to rescan it now
	PLAYLIST_ITEM_RENAMED		= 'pl\1\1'  // an item in the playlist got renamed
};


class _PlayListEntry;
class AudioFile;
struct file_info;


// Never EVER use this class directly!!!
// ALWAYS go through the PlaylistPtr class defined below.
class PrivatePlaylist
{
	public:
		status_t	RestoreFromMessage(const BMessage *mes, int32 index=0);
		void		SaveToMessage(BMessage &mes) const;

		void		MakeEmpty();
		int32		CountItems() const;
		int32		Add(const entry_ref &ref, int32 index= -1);
		int32		Add(const char *path, int32 index= -1);
		status_t	AddDir(const entry_ref &ref, int32 index=-1);
		status_t	AddDir(const char *path, int32 index=-1);
		status_t	InsertRefsFromMessage(BMessage *message, char *refname, int32 index);
		status_t	AddRefsFromMessage(BMessage *message, char *refname);
		status_t	SetName(int32 ID, const char *name);

		int32		IDForItemAt(int32 index) const;
		const char*	PathForItem(int32 ID) const;
		const char*	NameForItem(int32 ID) const;
		status_t	Remove(int32 ID);

		const char*	CurrentPath() const;
		const char*	CurrentName() const;
		
		status_t	Shuffle();
        status_t	Sort();
        status_t	SortByPath();
		
		int32		CurrentIndex() const;
		int32		CurrentID() const;
		status_t	GetInfo(int32 id, file_info *info);
		
		status_t	AddListener(BHandler *handler);
		status_t	RemoveListener(BHandler *handler);
		
		void 		Play(void);
		void 		Pause(void);
		float		Pitch(void);
		void		SetPitch(float);
		double		Position(void);
		void		SetPosition(double);
		float		Volume(void);
		void		SetVolume(float);
		void		PlayFile(int32 ID);
		void		PlayNext();
		void		PlayPrevious(bool reverse=false);
		
		bool		Lock();
		void		Unlock();
		bool		IsLocked();
		
		bool		HasControls();
		status_t	AddControls();
		bool		IsValid();

	private:
#ifdef PLAYLIST_SECRET_INNER_WORKINGS
PLAYLIST_SECRET_INNER_WORKINGS
#endif
		PrivatePlaylist();
		~PrivatePlaylist();

		void pInit();
		void pAddDirToList(const entry_ref &ref, int32 index=-1);
		void pAddPlayListToList(const entry_ref *ref, int32 index);
 static const char* pNameForItem(_PlayListEntry *);
 static const char* pPathForItem(_PlayListEntry *);
		_PlayListEntry *pItemForID(int32 ID) const;
		int32 pIndexForID(int32 ID) const;
		status_t pAddPlaylistEntry(_PlayListEntry *pe, int32 index);
		status_t pNotify(uint32 what, int32 who, int32 where);
		status_t pDropNotification();
		status_t pFlushNotification();
		void pCheckLock() const;
		void pSetControlSet(class audio*);
		status_t	SetCurrentIndex(int32 index);
		static int pSortFunc(const void *item1,const void *item2);
		static int pSortPathFunc(const void *item1,const void *item2);
		static int pShuffleFunc(const void *item1,const void *item2);

		sem_id	locksem;
		thread_id lockthread;
		int32	lockcount;
		bool isshuffled;
		int32 numitems;
		int32 currentindex;
		BList entrylist;
		BList listeners;
		BMessage notification;
		unsigned refcounter;
		AudioFile *audiofile;
		audio *controlset;
		_PlayListEntry *cache;
};


// A PlaylistPtr acts as a pointer to a playlist. You dereference a
// PlaylistPtr using regular pointer semantics to access the playlist's functions.
// Note that as long as you have a PlaylistPtr for a playlist, 
// that playlist will remain valid. Its associated controls might go
// away (i.e. the playlist gets removed from the controller/window),
// but you can still work with the files in the playlist, or add it
// to the controller again.
class PlaylistPtr
{
	public:
		PlaylistPtr(PrivatePlaylist* p=NULL);
		PlaylistPtr(const PlaylistPtr& p);
		PlaylistPtr(int32 ID);
		~PlaylistPtr();

		PrivatePlaylist* operator-> () { return ppl; }
		PrivatePlaylist& operator* ()  { return *ppl; }
		PlaylistPtr& operator= (const PlaylistPtr& p);
        bool        operator==(const PlaylistPtr &p) const;
        bool        operator!=(const PlaylistPtr &p) const;

	private:
		PrivatePlaylist* ppl;
};

#endif
