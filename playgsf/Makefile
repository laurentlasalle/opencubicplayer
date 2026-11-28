TOPDIR=../
include $(TOPDIR)Rules.make

comma:=,

# mGBA (https://mgba.io), built as a static library with only the Game Boy Advance
# core (see windows/build-deps.sh and macos/build-macos.sh), found through CFLAGS
# (-I<prefix>/include) and LDFLAGS (-L<prefix>/lib)
MGBA_LIBS?=-lmgba
ifeq ($(WINDOWS),1)
MGBA_LIBS+=-lws2_32 -lshlwapi -lole32 -luuid # sockets, paths, CoTaskMemFree, FOLDERID_* (mGBA platform code)
endif
ifneq ($(findstring darwin,$(host)),)
MGBA_LIBS+=-framework Foundation
endif

TARGETS=playgsf$(LIB_SUFFIX)

all: $(TARGETS)

clean:
	rm -f *.o *$(LIB_SUFFIX)

install:
	$(CP) playgsf$(LIB_SUFFIX) "$(DESTDIR)$(LIBDIROCP)/autoload/95-playgsf$(LIB_SUFFIX)"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIROCP)/autoload/95-playgsf$(LIB_SUFFIX)"

gsfemu.o: gsfemu.c \
	../config.h \
	../types.h \
	gsfemu.h
	$(CC) $< -o $@ -c

gsfinfo.o: gsfinfo.c \
	../config.h \
	../types.h \
	../cpiface/cpiface.h \
	../stuff/poutput.h \
	gsfemu.h \
	gsfload.h \
	gsfplay.h
	$(CC) $< -o $@ -c

gsfload.o: gsfload.c \
	../config.h \
	../types.h \
	gsfload.h
	$(CC) $< -o $@ -c

gsfplay.o: gsfplay.c \
	../config.h \
	../types.h \
	../boot/psetting.h \
	../cpiface/cpiface.h \
	../dev/mcp.h \
	../dev/player.h \
	../dev/ringbuffer.h \
	../filesel/dirdb.h \
	../filesel/filesystem.h \
	../stuff/err.h \
	../stuff/imsrtns.h \
	gsfemu.h \
	gsfload.h \
	gsfplay.h \
	gsfvis.h \
	../playgme/nsfvis.h
	$(CC) $< -o $@ -c

gsfpplay.o: gsfpplay.c \
	../config.h \
	../types.h \
	../boot/plinkman.h \
	../cpiface/cpiface.h \
	../dev/player.h \
	../filesel/dirdb.h \
	../filesel/filesystem.h \
	../filesel/mdb.h \
	../filesel/pfilesel.h \
	../stuff/compat.h \
	../stuff/err.h \
	../stuff/poutput.h \
	../stuff/sets.h \
	../playgme/gmeplay.h \
	gsfemu.h \
	gsfload.h \
	gsfplay.h \
	gsftype.h
	$(CC) $< -o $@ -c

gsftype.o: gsftype.c \
	../config.h \
	../types.h \
	../boot/plinkman.h \
	../filesel/filesystem.h \
	../filesel/mdb.h \
	../filesel/pfilesel.h \
	../stuff/err.h \
	gsfload.h \
	gsfplay.h \
	gsftype.h
	$(CC) $< -o $@ -c

gsfvis.o: gsfvis.c \
	../config.h \
	../types.h \
	../playgme/nsfvis.h \
	gsfemu.h \
	gsfvis.h
	$(CC) $< -o $@ -c

# tracker view code shared with playgme
gsfnsftrak.o: ../playgme/nsftrak.c \
	../config.h \
	../types.h \
	../cpiface/cpiface.h \
	../playgme/gmeplay.h \
	../playgme/nsfvis.h \
	../stuff/poutput.h
	$(CC) $< -o $@ -c

gsfnsfvis.o: ../playgme/nsfvis.c \
	../playgme/nsfvis.h
	$(CC) $(LIBGME_CFLAGS) $< -o $@ -c

playgsf_so=gsfemu.o gsfinfo.o gsfload.o gsfplay.o gsfpplay.o gsftype.o gsfvis.o gsfnsftrak.o gsfnsfvis.o
playgsf$(LIB_SUFFIX): $(playgsf_so)
	$(CC) $(SHARED_FLAGS) $(LDFLAGS) -o $@ $^ $(MGBA_LIBS) -lz -lm $(if $(filter 1,$(WINDOWS)),-Wl$(comma)--exclude-all-symbols)
