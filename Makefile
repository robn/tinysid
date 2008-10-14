CC = gcc
CFLAGS = -Wall -ggdb $(shell sdl-config --cflags) -DSDL -DSID_PLAYER -DPACKAGE=\"sidplayer\" -DVERSION=\"4.4\"
LDFLAGS = $(shell sdl-config --libs)

OBJECTS = cpu.o main.o main_sdl.o mem.o prefs.o prefs_items.o sid.o
HEADERS = cpu.h cpu_macros.h cpu_opcodes.h debug.h main.h mem.h prefs.h psid.h sid.h sys.h types.h

BINNAME = sidplayer

all: $(OBJECTS) $(HEADERS)
	$(CC) -o $(BINNAME) $(OBJECTS) $(LDFLAGS)

$(OBJECTS): $(HEADERS)

clean:
	rm -f $(OBJECTS) $(BINNAME)
