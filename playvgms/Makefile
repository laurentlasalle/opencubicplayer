TOPDIR=../
include $(TOPDIR)Rules.make

comma:=,

# vgmstream (https://vgmstream.org), built as a static library without FFmpeg, mpg123
# or Vorbis (see windows/build-deps.sh and macos/build-macos.sh), found through CFLAGS
# (-I<prefix>/include) and LDFLAGS (-L<prefix>/lib)
VGMSTREAM_LIBS?=-lvgmstream

TARGETS=playvgms$(LIB_SUFFIX)

all: $(TARGETS)

clean:
	rm -f *.o *$(LIB_SUFFIX)

install:
	$(CP) playvgms$(LIB_SUFFIX) "$(DESTDIR)$(LIBDIROCP)/autoload/95-playvgms$(LIB_SUFFIX)"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIROCP)/autoload/95-playvgms$(LIB_SUFFIX)"

vgmsfile.o: vgmsfile.c \
	../config.h \
	../types.h \
	../filesel/dirdb.h \
	../filesel/filesystem.h \
	vgmsplay.h
	$(CC) $< -o $@ -c

vgmsinfo.o: vgmsinfo.c \
	../config.h \
	../types.h \
	../cpiface/cpiface.h \
	../stuff/poutput.h \
	vgmsplay.h
	$(CC) $< -o $@ -c

vgmsplay.o: vgmsplay.c \
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
	vgmsplay.h
	$(CC) $< -o $@ -c

vgmspplay.o: vgmspplay.c \
	../config.h \
	../types.h \
	../boot/plinkman.h \
	../cpiface/cpiface.h \
	../dev/player.h \
	../filesel/dirdb.h \
	../filesel/filesystem.h \
	../filesel/mdb.h \
	../filesel/pfilesel.h \
	../stuff/err.h \
	../stuff/poutput.h \
	vgmsplay.h \
	vgmstype.h
	$(CC) $< -o $@ -c

vgmstype.o: vgmstype.c \
	../config.h \
	../types.h \
	../boot/plinkman.h \
	../filesel/dirdb.h \
	../filesel/filesystem.h \
	../filesel/mdb.h \
	../filesel/pfilesel.h \
	../stuff/err.h \
	vgmsplay.h \
	vgmstype.h
	$(CC) $< -o $@ -c

playvgms_so=vgmsfile.o vgmsinfo.o vgmsplay.o vgmspplay.o vgmstype.o
playvgms$(LIB_SUFFIX): $(playvgms_so)
	$(CC) $(SHARED_FLAGS) $(LDFLAGS) -o $@ $^ $(VGMSTREAM_LIBS) -lm $(if $(filter 1,$(WINDOWS)),-Wl$(comma)--exclude-all-symbols)
