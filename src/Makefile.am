## Process this file with automake to produce Makefile.in 

SUBDIRS = clamp_includes soundplay_includes

bin_PROGRAMS = sidplayer

sidplayer_SOURCES = main_sdl.cpp main.cpp main.h psid.h mem.cpp mem.h \
  prefs.cpp prefs_items.cpp prefs.h cpu.cpp cpu.h cpu_macros.h cpu_opcodes.h \
  sid.cpp sid.h sys.h types.h debug.h

EXTRA_sidplayer_SOURCES = cl_amp.cpp soundplay.cpp soundplay.h main_beos.cpp \
  prefs_window.cpp prefs_window.h

EXTRA_DIST = SIDPlayer.rsrc

# Create links to Frodo sources
main_sdl.cpp: cpu_macros.h cpu_opcodes.h prefs.h sid.cpp
cpu_macros.h cpu_opcodes.h prefs.h sid.cpp:
	ln -s $(top_srcdir)/../Frodo/src/$@ $@
