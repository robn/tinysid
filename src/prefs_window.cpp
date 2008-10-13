/*
 *  prefs_window.cpp - Preferences window
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

#include "prefs_window.h"
#include "prefs.h"


// Global variables
bool prefs_window_open = false;


// Message codes
const uint32 MSG_FILTERS = 'filt';
const uint32 MSG_SID_TYPE = 'sidt';
const uint32 MSG_EFFECT_NONE = 'enon';
const uint32 MSG_EFFECT_REVERB = 'ervb';
const uint32 MSG_EFFECT_SPATIAL = 'espt';


// Panning slider
class PanSlider : public BSlider {
public:
	PanSlider(BRect frame, const char *name, const char *prefs) : BSlider(frame, name, NULL, NULL, -0x100, 0x100, B_TRIANGLE_THUMB), prefs_name(prefs)
	{
		SetHashMarks(B_HASH_MARKS_TOP);
		SetHashMarkCount(3);
		const rgb_color bar_color = {128, 128, 216, 0};
		SetBarColor(bar_color);
		SetValue(PrefsFindInt32(prefs_name));
	}
	virtual ~PanSlider() {}
	virtual void SetValue(int32 value)
	{
		BSlider::SetValue(value);
		PrefsReplaceInt32(prefs_name, value);
	}

private:
	const char *prefs_name;
};


// Volume slider
class VolumeSlider : public BSlider {
public:
	VolumeSlider(BRect frame, const char *name, const char *prefs, int32 max = 0x180) : BSlider(frame, name, NULL, NULL, 0, max), prefs_name(prefs)
	{
		SetHashMarks(B_HASH_MARKS_NONE);
		const rgb_color fill_color = {128, 216, 128, 0};
		UseFillColor(true, &fill_color);
		SetValue(PrefsFindInt32(prefs_name));
	}
	virtual ~VolumeSlider() {}
	virtual void SetValue(int32 value)
	{
		BSlider::SetValue(value);
		PrefsReplaceInt32(prefs_name, value);
	}

private:
	const char *prefs_name;
};


/*
 *  Prefs window constructor
 */

PrefsWindow::PrefsWindow() : BWindow(BRect(0, 0, 400, 94), "SIDPlayer Sound Control", B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS)
{
	rgb_color fill_color = ui_color(B_PANEL_BACKGROUND_COLOR);
	Lock();
	BRect b = Bounds();

	// Move window to right position
	MoveTo(80, 80 + 124);

	// Light gray background
	BView *top = new BView(BRect(0, 0, b.right, b.bottom), "main", B_FOLLOW_NONE, B_WILL_DRAW);
	AddChild(top);
	top->SetViewColor(fill_color);

	// Audio effects popup menu
	BMenuField *menu_field;
	BPopUpMenu *menu = new BPopUpMenu("");
	menu_field = new BMenuField(BRect(8, 8, 180, 9), "audioeffect", "Effect", menu);
	menu_field->SetDivider(60);
	menu->AddItem(new BMenuItem("None", new BMessage(MSG_EFFECT_NONE)));
	menu->AddItem(new BMenuItem("Reverb", new BMessage(MSG_EFFECT_REVERB)));
	menu->AddItem(new BMenuItem("Spatial", new BMessage(MSG_EFFECT_SPATIAL)));
	top->AddChild(menu_field);
	menu->ItemAt(PrefsFindInt32("audioeffect"))->SetMarked(true);

	// Reverb feedback/delay sliders
	BStringView *label;
	top->AddChild(label = new BStringView(BRect(8, 32, 60, 51), "delay_title", "Delay"));
	label->SetViewColor(fill_color);
	top->AddChild(new VolumeSlider(BRect(60, 35, 180, 36), "delay", "revdelay", 500));
	top->AddChild(label = new BStringView(BRect(8, 52, 60, 71), "feedback_title", "Feedback"));
	label->SetViewColor(fill_color);
	top->AddChild(new VolumeSlider(BRect(60, 55, 180, 56), "feedback", "revfeedback", 0x100));

	// Filter enable/disable checkbox
	top->AddChild(filter_view = new BCheckBox(BRect(8, 75, 90, 90), "filters", "Filters", new BMessage(MSG_FILTERS)));
	filter_view->SetViewColor(fill_color);
	filter_view->SetValue(PrefsFindBool("filters") ? B_CONTROL_ON : B_CONTROL_OFF);

	// New SID chip checkbox
	top->AddChild(sid_type_view = new BCheckBox(BRect(90, 75, 200, 90), "sid_type", "New SID Chip", new BMessage(MSG_SID_TYPE)));
	sid_type_view->SetViewColor(fill_color);
	sid_type_view->SetValue(strcmp(PrefsFindString("sidtype"), "8580") ? B_CONTROL_OFF : B_CONTROL_ON);

	// Volume/panning sliders
	top->AddChild(label = new BStringView(BRect(190, 12, 203, 31), "v1_title", "1"));
	label->SetViewColor(fill_color);
	label->SetAlignment(B_ALIGN_CENTER);
	top->AddChild(label = new BStringView(BRect(190, 32, 203, 51), "v2_title", "2"));
	label->SetViewColor(fill_color);
	label->SetAlignment(B_ALIGN_CENTER);
	top->AddChild(label = new BStringView(BRect(190, 52, 203, 71), "v3_title", "3"));
	label->SetViewColor(fill_color);
	label->SetAlignment(B_ALIGN_CENTER);
	top->AddChild(label = new BStringView(BRect(190, 72, 203, 91), "v4_title", "4"));
	label->SetViewColor(fill_color);
	label->SetAlignment(B_ALIGN_CENTER);

	top->AddChild(label = new BStringView(BRect(204, 2, 304, 15), "panning_title", "Panning"));
	label->SetViewColor(fill_color);
	label->SetAlignment(B_ALIGN_CENTER);

	top->AddChild(new PanSlider(BRect(204, 15, 304, 16), "pan_0", "v1pan"));
	top->AddChild(new PanSlider(BRect(204, 35, 304, 36), "pan_1", "v2pan"));
	top->AddChild(new PanSlider(BRect(204, 55, 304, 56), "pan_2", "v3pan"));
	top->AddChild(new PanSlider(BRect(204, 75, 304, 76), "pan_3", "v4pan"));

	top->AddChild(label = new BStringView(BRect(309, 2, 397, 15), "volume_title", "Volume"));
	label->SetViewColor(fill_color);
	label->SetAlignment(B_ALIGN_CENTER);

	top->AddChild(new VolumeSlider(BRect(309, 15, 397, 16), "volume_0", "v1volume"));
	top->AddChild(new VolumeSlider(BRect(309, 35, 397, 36), "volume_1", "v2volume"));
	top->AddChild(new VolumeSlider(BRect(309, 55, 397, 56), "volume_2", "v3volume"));
	top->AddChild(new VolumeSlider(BRect(309, 75, 397, 76), "volume_3", "v4volume"));

	// Show window
	top->MakeFocus();
	Show();
	Unlock();
	prefs_window_open = true;
}


/*
 *  Prefs window asked to quit
 */

bool PrefsWindow::QuitRequested(void)
{
	prefs_window_open = false;
	return true;
}


/*
 *  Prefs window message handler
 */

void PrefsWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_FILTERS:
			PrefsReplaceBool("filters", filter_view->Value() == B_CONTROL_ON);
			break;

		case MSG_SID_TYPE:
			if (sid_type_view->Value() == B_CONTROL_ON)
				PrefsReplaceString("sidtype", "8580");
			else
				PrefsReplaceString("sidtype", "6581");
			break;

		case MSG_EFFECT_NONE:
			PrefsReplaceInt32("audioeffect", 0);
			break;
		case MSG_EFFECT_REVERB:
			PrefsReplaceInt32("audioeffect", 1);
			break;
		case MSG_EFFECT_SPATIAL:
			PrefsReplaceInt32("audioeffect", 2);
			break;
	}
}
