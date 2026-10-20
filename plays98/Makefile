TOPDIR=../
include $(TOPDIR)Rules.make

PKG_CONFIG?=pkg-config
comma:=,

# libvgm (https://github.com/ValleyBell/libvgm), found through pkg-config
LIBVGM_CFLAGS:=$(shell $(PKG_CONFIG) --cflags vgm-player vgm-emu vgm-utils 2>/dev/null)
LIBVGM_LIBS:=$(shell $(PKG_CONFIG) --libs vgm-player vgm-emu vgm-utils 2>/dev/null)

TARGETS=plays98$(LIB_SUFFIX)

all: $(TARGETS)

clean:
	rm -f *.o *$(LIB_SUFFIX)

install:
	$(CP) plays98$(LIB_SUFFIX) "$(DESTDIR)$(LIBDIROCP)/autoload/95-plays98$(LIB_SUFFIX)"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIROCP)/autoload/95-plays98$(LIB_SUFFIX)"

s98lib.o: s98lib.cpp s98lib.h
	$(CXX) $(LIBVGM_CFLAGS) $< -o $@ -c

s98play.o: s98play.c \
	../config.h \
	../types.h \
	../boot/psetting.h \
	../cpiface/cpiface.h \
	../dev/mcp.h \
	../dev/player.h \
	../dev/ringbuffer.h \
	../filesel/filesystem.h \
	../stuff/err.h \
	../stuff/imsrtns.h \
	s98lib.h \
	s98play.h
	$(CC) $< -o $@ -c

s98pplay.o: s98pplay.c \
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
	s98play.h \
	s98type.h
	$(CC) $< -o $@ -c

s98type.o: s98type.c \
	../config.h \
	../types.h \
	../boot/plinkman.h \
	../filesel/filesystem.h \
	../filesel/mdb.h \
	../filesel/pfilesel.h \
	../stuff/err.h \
	s98type.h
	$(CC) $< -o $@ -c

plays98_so=s98type.o s98play.o s98pplay.o s98lib.o
plays98$(LIB_SUFFIX): $(plays98_so)
	$(CXX) $(SHARED_FLAGS) -o $@ $^ $(LIBVGM_LIBS) $(ICONV_LIBS) -lz $(if $(filter 1,$(WINDOWS)),-Wl$(comma)--exclude-all-symbols)
