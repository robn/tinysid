/*
 *  main_beos.cpp - SIDPlayer BeOS main program
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
#include <InterfaceKit.h>
#include <storage/Path.h>
#include <media/SoundPlayer.h>

#include <stdio.h>

#include "main.h"
#include "prefs_window.h"
#include "sid.h"


// Message codes
const uint32 MSG_NEW_MODULE = 'load';
const uint32 MSG_NEW_SONG = 'song';
const uint32 MSG_SHOW_PREFS = 'pref';
const uint32 MSG_PLAY_PAUSE = 'plpa';
const uint32 MSG_STOP = 'stop';
const uint32 MSG_NEXT = 'next';
const uint32 MSG_PREV = 'prev';


class MainWindow;
class SpeedSlider;

// Application object
class SIDPlayer : public BApplication {
public:
	SIDPlayer();
	virtual ~SIDPlayer();
	virtual void ArgvReceived(int32 argc, char **argv);
	virtual void RefsReceived(BMessage *msg);
	virtual void MessageReceived(BMessage *msg);
	virtual void ReadyToRun(void);
	virtual void AboutRequested(void);

private:
	static void buffer_proc(void *cookie, void *buffer, size_t size, const media_raw_audio_format &format);

	MainWindow *main_window;
	PrefsWindow *prefs_window;
	BSoundPlayer player;
	bool player_stopped;
};


// Main window object
class MainWindow : public BWindow {
public:
	MainWindow();
	virtual bool QuitRequested(void);
	virtual void MessageReceived(BMessage *msg);

private:
	BStringView *make_name_display(BRect frame, char *label_text, BView *parent);

	rgb_color fill_color;

	BStringView *name_view;
	BStringView *author_view;
	BStringView *copyright_view;
	BStringView *position_view;
	SpeedSlider *speed_slider;
};


// Top view object (handles drag&drop)
class TopView : public BView {
public:
	TopView(BRect frame, const char *name, uint32 resizingMode, uint32 flags);
	virtual void MessageReceived(BMessage *msg);
	virtual void KeyDown(const char *bytes, int32 numBytes);
};


// Buttons
class PlayPauseButton : public BButton {
public:
	PlayPauseButton(BRect frame, BMessage *msg);
	virtual void Draw(BRect update);
};

class StopButton : public BButton {
public:
	StopButton(BRect frame, BMessage *msg);
	virtual void Draw(BRect update);
};

class NextButton : public BButton {
public:
	NextButton(BRect frame, BMessage *msg);
	virtual void Draw(BRect update);
};

class PrevButton : public BButton {
public:
	PrevButton(BRect frame, BMessage *msg);
	virtual void Draw(BRect update);
};


// Speed slider
class SpeedSlider : public BSlider {
public:
	SpeedSlider(BRect frame, const char *name) : BSlider(frame, name, "Speed", NULL, -100, 100, B_TRIANGLE_THUMB)
	{
		SetHashMarks(B_HASH_MARKS_TOP);
		SetHashMarkCount(3);
		const rgb_color bar_color = {128, 128, 216, 0};
		SetBarColor(bar_color);
		SetValue(0);
	}
	virtual ~SpeedSlider() {}
	virtual void SetValue(int32 value)
	{
		int percent = value < 0 ? 100 + value / 2 : 100 + value;
		sprintf(status, "%d%%", percent);
		SIDAdjustSpeed(percent);
		BSlider::SetValue(value);
	}
	virtual char *UpdateText(void) const
	{
		return status;
	}

private:
	mutable char status[32];
};


/*
 *  Application constructor
 */

#if B_HOST_IS_LENDIAN
const media_raw_audio_format audio_format = {44100.0, 2, media_raw_audio_format::B_AUDIO_SHORT, B_MEDIA_LITTLE_ENDIAN, 4096};
#else
const media_raw_audio_format audio_format = {44100.0, 2, media_raw_audio_format::B_AUDIO_SHORT, B_MEDIA_BIG_ENDIAN, 4096};
#endif

SIDPlayer::SIDPlayer() : BApplication("application/x-vnd.cebix-SIDPlayer"), player(&audio_format, "SIDPlayer", buffer_proc)
{
	main_window = NULL;
	player.SetHasData(true);
	player_stopped = true;
}


/*
 *  Application destructor
 */

SIDPlayer::~SIDPlayer()
{
	main_window = NULL;
	prefs_window = NULL;
	player.Stop();
	player_stopped = true;
}


/*
 *  Shell arguments received
 */

void SIDPlayer::ArgvReceived(int32 argc, char **argv)
{
	if (argc < 2)
		return;

	for (int i=1; i<argc; i++) {
		if (argv[i][0] == '-')
			continue;
		player.Stop();
		LoadPSIDFile(argv[i]);
		player.Start();
		player_stopped = false;
		if (main_window)
			main_window->PostMessage(MSG_NEW_MODULE);
	}
}


/*
 *  Tracker arguments received
 */

void SIDPlayer::RefsReceived(BMessage *msg)
{
	entry_ref the_ref;
	if (msg->FindRef("refs", &the_ref) == B_NO_ERROR) {
		BEntry the_entry;
		if (the_entry.SetTo(&the_ref) == B_NO_ERROR) {
			if (the_entry.IsFile()) {
				BPath the_path;
				the_entry.GetPath(&the_path);
				player.Stop();
				LoadPSIDFile(the_path.Path());
				player.Start();
				player_stopped = false;
				if (main_window)
					main_window->PostMessage(MSG_NEW_MODULE);
			}
		}
	}
}


/*
 *  Message received
 */

void SIDPlayer::MessageReceived(BMessage *msg)
{
	switch (msg->what) {

		case B_SIMPLE_DATA:	// Dropped message
			RefsReceived(msg);
			break;

		case MSG_SHOW_PREFS:
			if (!prefs_window_open)
				prefs_window = new PrefsWindow();
			else if (prefs_window)
				prefs_window->Activate(true);
			break;

		case MSG_PLAY_PAUSE:
			if (player_stopped) {
				player.Start();
				player_stopped = false;
			} else {
				player.Stop();
				player_stopped = true;
			}
			break;

		case MSG_STOP:
			player.Stop();
			player_stopped = true;
			SelectSong(current_song);
			main_window->PostMessage(MSG_NEW_SONG);
			break;

		case MSG_NEXT:
			if (current_song < number_of_songs-1) {
				player.Stop();
				SelectSong(current_song + 1);
				main_window->PostMessage(MSG_NEW_SONG);
				if (!player_stopped)
					player.Start();
			}
			break;

		case MSG_PREV:
			if (current_song > 0) {
				player.Stop();
				SelectSong(current_song - 1);
				main_window->PostMessage(MSG_NEW_SONG);
				if (!player_stopped)
					player.Start();
			}
			break;

		default:
			BApplication::MessageReceived(msg);
			break;
	}
}


/*
 *  Arguments processed, open player window
 */

void SIDPlayer::ReadyToRun(void)
{
	main_window = new MainWindow();
	if (IsPSIDLoaded())
		main_window->PostMessage(MSG_NEW_MODULE);
}


/*
 *  Show About window
 */

void AboutWindow()
{
	BAlert *theAlert = new BAlert("",
			"SIDPlayer\nVersion " VERSION "\n\n"
			"Copyright " B_UTF8_COPYRIGHT " 1996-2004 Christian Bauer\n"
			"E-mail: Christian.Bauer@uni-mainz.de\n"
			"http://www.uni-mainz.de/~bauec002/SPMain.html\n\n"
			"SIDPlayer comes with ABSOLUTELY NO\n"
			"WARRANTY. This is free software, and\n"
			"you are welcome to redistribute it\n"
			"under the terms of the GNU General\n"
			"Public License.\n",
			"OK", NULL, NULL, B_WIDTH_FROM_LABEL);

	BTextView *theText = theAlert->TextView();
	if (theText) {
		theText->SetStylable(true);
		theText->Select(0, 9);
		BFont ourFont;
		theText->SetFontAndColor(be_bold_font);
		theText->GetFontAndColor(2, &ourFont, NULL);
		ourFont.SetSize(24);
		theText->SetFontAndColor(&ourFont);
	}
	theAlert->Go();
}

void SIDPlayer::AboutRequested(void)
{
	AboutWindow();
}


/*
 *  SoundPlayer buffer procedure
 */

void SIDPlayer::buffer_proc(void *cookie, void *buffer, size_t size, const media_raw_audio_format &format)
{
	SIDCalcBuffer((uint8 *)buffer, size);
}


/*
 *  Main window constructor
 */

MainWindow::MainWindow() : BWindow(BRect(0, 0, 284, 96), "SIDPlayer", B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS)
{
	fill_color = ui_color(B_PANEL_BACKGROUND_COLOR);
	BRect b = Bounds();

	// Move window to right position
	Lock();
	MoveTo(80, 80);

	// Create menu bar
	BMenuBar *bar = new BMenuBar(BRect(0, 0, b.right, b.bottom), "menubar");
	BMenu *menu = new BMenu("File");
	menu->AddItem(new BMenuItem("About SIDPlayer" B_UTF8_ELLIPSIS, new BMessage(B_ABOUT_REQUESTED)));
	menu->AddItem(new BSeparatorItem());
	menu->AddItem(new BMenuItem("Sound Control" B_UTF8_ELLIPSIS, new BMessage(MSG_SHOW_PREFS), 'P'));
	menu->AddItem(new BSeparatorItem());
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(be_app);
	bar->AddItem(menu);
	AddChild(bar);
	SetKeyMenuBar(bar);
	float menu_height = bar->Bounds().Height();

	// Resize window to fit menu bar
	ResizeBy(0, menu_height);

	// Light gray background
	TopView *top = new TopView(BRect(0, menu_height, b.right, b.bottom + menu_height), "main", B_FOLLOW_NONE, B_WILL_DRAW);
	AddChild(top);
	top->SetViewColor(fill_color);

	// Name/author/copyright display
	name_view = make_name_display(BRect(0, 5, 279, 21), "Name", top);
	author_view = make_name_display(BRect(0, 25, 279, 41), "Author", top);
	copyright_view = make_name_display(BRect(0, 45, 279, 61), "Copyright", top);

	// Buttons
	top->AddChild(new PlayPauseButton(BRect(6, 67, 36, 91), new BMessage(MSG_PLAY_PAUSE)));
	top->AddChild(new StopButton(BRect(37, 67, 67, 91), new BMessage(MSG_STOP)));
	top->AddChild(new PrevButton(BRect(68, 67, 98, 91), new BMessage(MSG_PREV)));
	top->AddChild(new NextButton(BRect(99, 67, 129, 91), new BMessage(MSG_NEXT)));

	// Position indicator
	top->AddChild(position_view = new BStringView(BRect(134, 72, 193, 85), "position", ""));
	position_view->SetViewColor(fill_color);

	// Speed slider
	top->AddChild(speed_slider = new SpeedSlider(BRect(194, 62, 279, 63), "speed"));

	// Show window
	top->MakeFocus();
	Unlock();
	Show();
}


/*
 *  Create name display field
 */

BStringView *MainWindow::make_name_display(BRect frame, char *label_text, BView *parent)
{
	// Label to the left of the display field
	BRect label_rect = frame;
	label_rect.right = label_rect.left + 65;

	BStringView *label = new BStringView(label_rect, "", label_text);
	parent->AddChild(label);
	label->SetViewColor(fill_color);
	label->SetLowColor(fill_color);
	label->SetAlignment(B_ALIGN_RIGHT);
	label->SetFont(be_bold_font);

	// Box around display field
	BRect frame_rect = frame;
	frame_rect.left += 70;

	BBox *box = new BBox(frame_rect);
	parent->AddChild(box);
	box->SetViewColor(fill_color);
	box->SetLowColor(fill_color);

	// The display field
	BRect textview_rect = frame_rect;
	textview_rect.OffsetTo(0, 0);
	textview_rect.InsetBy(4, 2);

	BStringView *text = new BStringView(textview_rect, "", "");
	box->AddChild(text);
	text->SetViewColor(fill_color);
	text->SetLowColor(fill_color);
	text->SetFont(be_plain_font);
	return text;
}


/*
 *  Main window closed, quit program
 */

bool MainWindow::QuitRequested(void)
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/*
 *  Message received
 */

void MainWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_NEW_MODULE:
			// Update text views
			Lock();
			name_view->SetText(module_name);
			author_view->SetText(author_name);
			copyright_view->SetText(copyright_info);
			Unlock();
			// falls through

		case MSG_NEW_SONG:
			// Update position indicator and speed slider
			if (number_of_songs > 0) {
				char str[16];
				sprintf(str, "Song %d/%d", current_song + 1, number_of_songs);
				position_view->SetText(str);
			}
			speed_slider->SetValue(0);
			break;

		case MSG_SHOW_PREFS:
		case MSG_PLAY_PAUSE:
		case MSG_STOP:
		case MSG_NEXT:
		case MSG_PREV:
			be_app->PostMessage(msg);
			break;

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


/*
 *  TopView handles dropped messages (load new PSID module) and keypresses
 */

TopView::TopView(BRect frame, const char *name, uint32 resizingMode, uint32 flags)
 : BView(frame, name, resizingMode, flags) {}

void TopView::MessageReceived(BMessage *msg)
{
	if (msg->what == B_SIMPLE_DATA)
		be_app->PostMessage(msg);
	else
		BView::MessageReceived(msg);
}

void TopView::KeyDown(const char *bytes, int32 numBytes)
{
	BMessage *msg = Window()->CurrentMessage();
	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32 *)&modifiers);

	switch (bytes[0]) {
		case 'p':
		case 'P':
			Window()->PostMessage(MSG_PLAY_PAUSE);
			break;

		case B_ESCAPE:
		case B_SPACE:
		case 's':
		case 'S':
			Window()->PostMessage(MSG_STOP);
			break;

		case B_LEFT_ARROW:
			Window()->PostMessage(MSG_PREV);
			break;

		case B_RIGHT_ARROW:
		case 'n':
		case 'N':
			Window()->PostMessage(MSG_NEXT);
			break;

		case 'q':
		case 'Q':
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
	}
}


/*
 *  Play/Pause button
 */

PlayPauseButton::PlayPauseButton(BRect frame, BMessage *msg) : BButton(frame, "play", "", msg) {};

void PlayPauseButton::Draw(BRect update)
{
	// First draw normal button
	BButton::Draw(update);

	// Then draw play/pause image on top of it
	if (Value())
		SetHighColor(255, 255, 255);
	else
		SetHighColor(0, 0, 0);

	FillRect(BRect(11, 8, 13, 16));
	FillTriangle(BPoint(16, 8), BPoint(16, 16), BPoint(20, 12));
}


/*
 *  Stop button
 */

StopButton::StopButton(BRect frame, BMessage *msg) : BButton(frame, "stop", "", msg) {};

void StopButton::Draw(BRect update)
{
	// First draw normal button
	BButton::Draw(update);

	// Then draw stop image on top of it
	if (Value())
		SetHighColor(255, 255, 255);
	else
		SetHighColor(0, 0, 0);

	FillRect(BRect(11, 8, 20, 16));
}


/*
 *  "Next" button
 */

NextButton::NextButton(BRect frame, BMessage *msg) : BButton(frame, "next", "", msg) {};

void NextButton::Draw(BRect update)
{
	// First draw normal button
	BButton::Draw(update);

	// Then draw "next" image on top of it
	if (Value())
		SetHighColor(255, 255, 255);
	else
		SetHighColor(0, 0, 0);

	FillTriangle(BPoint(12, 8), BPoint(12, 16), BPoint(16, 12));
	FillRect(BRect(17, 8, 19, 16));
}


/*
 *  "Prev" button
 */

PrevButton::PrevButton(BRect frame, BMessage *msg) : BButton(frame, "prev", "", msg) {};

void PrevButton::Draw(BRect update)
{
	// First draw normal button
	BButton::Draw(update);

	// Then draw "prev" image on top of it
	if (Value())
		SetHighColor(255, 255, 255);
	else
		SetHighColor(0, 0, 0);

	FillRect(BRect(12, 8, 14, 16));
	FillTriangle(BPoint(19, 8), BPoint(19, 16), BPoint(15, 12));
}


/*
 *  Get current value of microsecond timer
 */

uint64 GetTicks_usec()
{
	return system_time();
}


/*
 *  Delay by specified number of microseconds (<1 second)
 */

void Delay_usec(uint32 usec)
{
	snooze(usec);
}


/*
 *  Main program
 */

int main(int argc, char **argv)
{
	InitAll(argc, argv);
	SIDPlayer *the_app = new SIDPlayer();
	the_app->Run();
	delete the_app;
	ExitAll();
	return 0;
}
