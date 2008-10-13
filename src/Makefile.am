## Process this file with automake to produce Makefile.in 

bin_PROGRAMS = sidplayer

sidplayer_SOURCES = main_sdl.cpp main.cpp main.h psid.h mem.cpp mem.h \
  prefs.cpp prefs_items.cpp prefs.h cpu.cpp cpu.h cpu_macros.h cpu_opcodes.h \
  sid.cpp sid.h sys.h types.h debug.h

# Create links to Frodo sources
main_sdl.cpp: cpu_macros.h cpu_opcodes.h prefs.h sid.cpp
cpu_macros.h cpu_opcodes.h prefs.h sid.cpp:
	ln -s $(top_srcdir)/../Frodo/src/$@ $@
