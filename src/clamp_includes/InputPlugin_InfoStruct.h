#ifndef CLAMP_INPUT_PLUGIN_INFO_H
#define CLAMP_INPUT_PLUGIN_INFO_H

#define MAX_INPLUG_FNAME_LEN 100

// The following is for the Flags member in PlayerInfoStruct
// (File Position Promille => 0=start, and 1000=end of file)
#define INPLUG_NO_TOTTIME       0x0001  // The plugin can't estimate a total time for the current file
#define INPLUG_NO_CURRTIME      0x0002  // The plugin will not provide any information in CurrTime
#define INPLUG_JUMP_OK          0x0004  // The plugin can jump around in the current song
                                        // Observe: Jumping can not be done if INPLUG_NO_CURRTIME is given!!
#define INPLUG_JUMP_FILEPOS     0x0008  // Makes JumpTo() give the new position in file position promille
                                        // instead of milliseconds as it is default
#define INPLUG_CURRTIME_FILEPOS 0x0010  // The plugin will set CurrTime to the current file position promille
                                        // instead of milliseconds as it is default
#define INPLUG_HANDLE_SPEED     0x0020  // CL-Amp will call NewSpeed() to ask for a new speed and will not
                                        // try to adjust it by itself!
                                        //  (CL-Amp is otherwise taking care of speed adjustment by itself!)
#define INPLUG_HANDLE_VOLUME    0x0040  // CL-Amp will call NewVolume() to ask for a new speed and will not
                                        // try to adjust it by itself!
                                        //  (CL-Amp is otherwise taking care of volume adjustment by itself!)
#define INPLUG_INDEPENDENT      0x0080  // The plugin will remain in GetAudio() while playing this song
                                        //  * CL-Amp will call JumpTo() from thread A instead of thread B
                                        //  * CL-Amp will call NewSpeed() from thread A instead of thread B
                                        //    (NewSpeed() is only called if the INPLUG_HANDLE_SPEED flag is given!)

struct PlayerInfoStruct {
	char Title[MAX_INPLUG_FNAME_LEN]; // If there is a better title than the filename it can be given here!
                                      // Example: A mp3 plugin could read it from the Id tag...
                                      // Leave it blank to tell CL-Amp to make a title from the filename!
	unsigned long Flags;
	long CurrTime, TotTime; // milliSeconds!
	long BitRate, Frequency;
	long BufferVolume; // Promille, Buffer is used by the http handler
	bool Stereo;
	bool BufferOn, PreBuffering; // Buffer is used by the http handler
	bool SampleIsOnly8Bits;
	short Fader;
	float *Equalizer;
	bool NewEqValues, dummy;
	long SongId;		// CL-Amp is setting a Song identification here.
						// some special plugins may need it...
	long Future[11];
};

#endif
