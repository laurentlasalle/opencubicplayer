Open Cubic Player v3.5.0 - Omni v8 (unofficial Windows x64 build)
==================================================================
Omni is an unofficial build of Open Cubic Player by Stian Skjelstad
(https://github.com/mywave82/opencubicplayer), based on its git master of
2026-10-17 (version 3.5.0), with the extra features listed below.
Source and releases: https://github.com/laurentlasalle/opencubicplayer
Please report problems of this build there, not to the upstream project.

Versions are named <upstream version>-omni.<release>, e.g. 3.5.0-omni.8.

Start: double-click ocp.exe (SDL3 window, no console window).
Requires Windows 10 or 11, 64-bit.

Log file (diagnostics)
----------------------
On every start, OCP writes its messages to
    %APPDATA%\OpenCubicPlayer\ocp-log.txt
and keeps the log of the previous run in ocp-log-previous.txt.
Every startup step is written with a timestamp (configuration, each plugin,
SDL video, fonts, window, audio, file browser), and "normal exit" on exit.
If OCP hangs or crashes, the last line tells where. If another OCP instance
is already running, the log goes to ocp-log-second-instance.txt, with a
warning at the top.

Window size / 1080p and 2160p screens
-------------------------------------
Default: 128 columns x 48 rows (8x16 font, 1024x768 at scale 1).
The window is enlarged by an integer factor according to the Windows display
scaling of the screen it is on: 100-125 % -> x1, 150-249 % -> x2,
250 % and more -> x3. When the window is moved from a 1080p screen (100 %)
to a 2160p screen (150 % or 200 %), it doubles in size and keeps the same
number of columns and rows (doubled pixels, no blur).

Settings in the [screen] section of the user's ocp.ini:
    %APPDATA%\OpenCubicPlayer\Config\ocp.ini
This file is copied from etc\ocp.ini on the first start. If OCP was already
started with a previous version, edit or delete it.
    screentype=0   80x25 (8x16)     screentype=2   80x50 (8x8, like DOS)
    screentype=1   80x30 (8x16)     screentype=3   80x60 (8x8)
    screentype=4..7  128x48, 160x64, 128x96, 160x128     8 = custom
    sdlscale=0     automatic, per screen; 1, 2 or 3 to force a factor

Automatic advance to the next file of a directory ([fileselector])
------------------------------------------------------------------
When a file started from the file browser (Enter in the left list) ends and
the playlist is empty, OCP plays the next file of the same directory, in the
displayed order, whatever its format (S3M, NSF, MOD...). After the last file
of the directory, OCP returns to the file browser.
During playback, Enter skips to the next file of the directory.
    autonext=on    (off for the original behaviour)
    loop=off       required: with loop=on (Ctrl+L) the file loops forever
                   and nothing advances
If the playlist (right pane) holds files, it has priority, as before.

Track length of NSF, GBS, KSS, VGM... (section [gme])
----------------------------------------------------
An NSF file does not store the length of its tracks: without a limit they
loop forever. Each track now plays for a given time, fades out, then the
next track of the file starts (and the next file after the last track).
    defaultlength=150   seconds when the file gives no length (0 = forever)
    loops=2             loop count when the file gives intro + loop (VGM...)
    fade=8              seconds of fade-out
When the file provides a length (NSFe, SPC...), it is used.
The [gme] section can be added as is to the ocp.ini in %APPDATA%; otherwise
these defaults apply. These settings can also be changed inside OCP: in the
file browser, go to setup: and open gmeconfig.dev (arrows, Esc to save).

Tracker view for NSF / NSFe / HES / GBS (section [gme])
-------------------------------------------------------
While an NSF (NES), HES (PC Engine) or GBS (Game Boy) file plays, the
"pattern" view (key t) shows the channels like a tracker (FamiTracker,
MOD/S3M). It is rebuilt from the sound chip registers, read 60 times per
second (50 for PAL), and follows exactly what is heard.
 - The number/channel mapping is shown in the title of the view
   (1:P1 2:P2 3:Tri 4:Noi 5:DMC ...).
 - NSF: one column per channel: Pulse 1, Pulse 2, Triangle, Noise, DPCM,
   then the expansion chips (VRC6, VRC7, FDS, MMC5, N163, Sunsoft 5B).
   Instrument = pulse duty cycle, noise mode, VRC7 patch; volume 0-F.
 - HES: 6 channels (W1-W4, M1-M2). Instrument = waveform number, in order
   of appearance (01, 02...); volume 00-1F. Channels 5-6 in noise mode:
   "X-#". DDA mode (samples, drums): "***".
 - GBS: 4 channels (Sq1, Sq2, Wav, Noi). Instrument = duty cycle (squares),
   waveform number (Wav), short noise mode (01).
 - One row = 6 frames (1/10 s) by default. Each cell shows the note (or the
   noise frequency "X-#", the DPCM sample "Sxx"), the instrument and the
   volume. "---" = the channel went silent. Notes come from the actual
   frequencies.
 - The columns share the full width of the screen, whatever the number of
   channels (4, 5, 6, 8...). When there is room, a 4th field shows that the
   frequency moves during the row: ^ up, v down, ~ vibrato (CP437 arrows
   and symbol on screen). A vibrato or portamento does not repeat the note.
   With many channels the volume has a single digit, and beyond about 9
   channels on 80 columns, OCP falls back to its usual layouts (channels
   scroll). Tab / PgUp / PgDn select another layout; Home returns to this one.
 - Keys 1 to 9 and 0: mute/unmute a channel; q and s as usual. (These keys
   also work with the other GME formats: SPC, VGM..., one key per emulator
   voice.)
    trackerview=on   off for the original display
    rowframes=6      frames per row (1 to 32); 3 = more detail, 12 = more compact
The other GME formats (SPC, VGM, KSS...) have no tracker view.

S98 files (NEC PC-88/PC-98, Sharp X1/X68000) - section [s98]
-----------------------------------------------------------
Played by the new plays98 player, based on libvgm (Valley Bell), which
emulates the Yamaha chips of these machines: YM2203 (OPN), YM2608 (OPNA)
with its SSG part, YM2151 (OPM), YM2612, OPLL/OPL... and AY-3-8910/SN76489.
Title, artist and game are read from the file (Shift-JIS or UTF-8), the
length is shown in the file browser, the chips in the comment field.
The file gives its loop point: a looping track plays
    loops=2    times the looping part, then
    fade=8     seconds of fade-out.
A track without a loop ends by itself. With OCP's loop mode (Ctrl+L), the
track loops forever. Ctrl+Home restarts the track.

Unifont fonts (recommended)
---------------------------
GNU Unifont is not included. OCP works without it, but characters outside
ASCII/CP437 will be displayed badly. Download the three OTF files from
https://unifoundry.com/unifont/ and copy them into "data" with these names:
    data\unifont.otf   data\unifont_csur.otf   data\unifont_upper.otf

If ocp.exe reports that VCRUNTIME140.dll is missing, install the
"Microsoft Visual C++ Redistributable x64" (required by discid.dll).

Release history
---------------
Omni v1 to v6 were distributed as "3.5.1" to "3.5.6".
Omni v8 (3.5.0-omni.8)
 - Tracker view: the upcoming rows are filled in advance (a second, silent
   emulator runs a few seconds ahead of the audio)
 - Channel names in the column header of the tracker view, shorter title
 - 128x48 text mode by default (screentype=4)
Omni v7 (3.5.0-omni.7, 2026-10-19)
 - Versions named <upstream version>-omni.<release>; OCP itself reports the
   upstream version again (3.5.0), the title bar shows "Omni v7"
 - Documentation and log messages in English; source published on GitHub
Omni v6 (2026-10-19)
 - Tracker view also for HES (PC Engine) and GBS (Game Boy)
 - Vibrato and portamento: indicator only, no repeated notes
Omni v5 (2026-10-19)
 - NSF tracker view no longer freezes after several files in a row
 - Columns stretched over the full width, according to the channel count
 - Pitch slide indicator (^ v ~) in each cell
Omni v4 (2026-10-19)
 - Tracker view for NSF/NSFe, synchronized with the sound (trackerview, rowframes)
 - Mute/unmute voices (keys 1-9) for all GME formats
 - Setup dialog setup:gmeconfig.dev (tracker view, length, loops, fade)
 - libgme.dll: reads the NSF sound registers (see notes)
Omni v3 (2026-10-19)
 - ocp.exe without console window (Windows GUI application)
 - Automatic log ocp-log.txt / ocp-log-previous.txt with timestamped steps
 - ocp-journal.bat removed (replaced by the automatic log)
Omni v2 (2026-10-18)
 - New S98 player (plays98) based on libvgm: PC-88/PC-98, X1, X68000
 - [s98] section in ocp.ini (loops, fade)
Omni v1 (2026-10-18)
 - HiDPI scaling: x2 on a 2160p screen, follows screen changes (sdlscale)
 - 80x30 mode by default (screentype=1), mode comments fixed
 - GME (NSF, GBS, KSS, VGM...): default length, loops and fade ([gme])
 - Keys < and >: fixed for the first and last track of a file
 - Automatic advance to the next file of a directory (autonext)
3.5.0
 - First Windows build of upstream 3.5.0 (cross-compiled with llvm-mingw)

Build notes
-----------
Cross-compiled from Linux with llvm-mingw 20261006 (clang, UCRT); see the
"windows" directory of the source repository (fetch-sources.sh,
build-deps.sh, build-ocp.sh, package.sh). Changes to the upstream sources:
 - configure: the clang target "x86_64-w64-windows-gnu" is handled as MinGW
 - boot/plinkman.c/.h: static plugin list through grouped PE sections
   "plugin_list$a/$m/$z" (lld provides no __start_/__stop_ symbols on PE)
 - playsid/libsidplayfp-api.cpp: #include <cstdlib>
 - Makefile.in: plays98 built when libvgm is visible to pkg-config;
   ocp.exe linked as a Windows GUI application
 - stuff/poutput-sdl3.c: integer HiDPI scaling (sdlscale option), follows
   SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED
 - filesel/pfilesel.c: automatic advance in the directory (autonext)
 - boot/kickload-windows.c: stderr/stdout redirected to ocp-log.txt;
   stuff/milestone.h (new) + timestamped startup steps in boot/pmain.c,
   boot/plinkman.c, stuff/poutput-sdl3.c, devp/devpsdl3.c, filesel/pfilesel.c
 - playgme: track length/fade ([gme]), keys < and > fixed, voice muting,
   tracker view (nsfvis.c, nsftrak.c), setup dialog (gmeconfig.c)
 - cpiface: "stretch" mode of the pattern view (TrackStretch), used only by
   the GME tracker view
 - plays98/ (new): S98 player; libvgm is linked statically into
   95-plays98.dll. libvgm has no global license: each emulation core has
   its own (MAME BSD/GPL, Nuked LGPL...). Check before redistributing.
 - ocp.ini: screentype=4 (128x48), autonext, [gme] and [s98] sections
 - omni-version.h (new): Omni release number, shown in the title bar;
   filesel/mdb.c rescans files of unknown type after an Omni update
 - libgme (game-music-emu, separate repository): new API gme_vis_enable /
   gme_vis_read (NSF, HES, GBS) that copies the sound chip registers once per
   frame
iconv is provided by win-iconv instead of GNU libiconv.
