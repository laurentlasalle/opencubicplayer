Open Cubic Player v3.5.0 - Omni v10 (unofficial Windows x64 build)
===================================================================
Omni is an unofficial build of Open Cubic Player by Stian Skjelstad
(https://github.com/mywave82/opencubicplayer), based on its git master of
2026-10-17 (version 3.5.0), with the extra features listed below.
Source and releases: https://github.com/laurentlasalle/opencubicplayer
Please report problems of this build there, not to the upstream project.

Versions are named <upstream version>-omni.<release>, e.g. 3.5.0-omni.10.

Start: double-click ocp.exe (SDL3 window, no console window).
Requires Windows 10 or 11, 64-bit.

Keys: F1 opens the manual; Alt+K lists the keys available on the current
screen (file browser, playback, tracker view), always up to date; Alt+C
opens the basic setup.

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
displayed order, whatever its format (S3M, NSF, MOD and others). After the last file
of the directory, OCP returns to the file browser.
During playback, Enter skips to the next file of the directory.
    autonext=on    (off for the original behaviour)
    loop=off       required: with loop=on (Ctrl+L) the file loops forever
                   and nothing advances
If the playlist (right pane) holds files, it has priority, as before.

Setup of the players in ALT-C (file browser)
--------------------------------------------
Below the file browser settings, ALT-C now lists the ocp.ini settings of
the players added by Omni, in one place: SID (song length database, end
when the tune starts again, default length, fade-out), volume
normalization, S98 (loops, fade-out) and game music (tracker view, frames
per row, default length, loops, fade-out). Press the letter of an item:
on/off items switch, the others ask for a number or, for the song length
database, a file path (TAB completes the names). A change applies to the
next file opened; ALT-S saves everything to ocp.ini. On a small screen
(80x25), the player setup is on a second page: TAB goes from one page to
the other. The same settings stay in the dialogs of the setup: folder
(sidconfig.dev, gmeconfig.dev).

Shuffle play (ALT-C, item 2; [fileselector] randomplay=)
--------------------------------------------------------
    off       files in order
    on        each file of the directory (or of the playlist) once, in a
              random order, then playback stops
    endless   new shuffled rounds, without end; a round never starts with
              the file that ended the previous one
Shuffle applies both to the automatic advance in a directory and to the
playlist. In a directory, "previous" (Ctrl+Left / <) goes back to the file
played before. In endless mode, played files stay in the playlist.

Track length of NSF, GBS, KSS, VGM and others (section [gme])
------------------------------------------------------------
An NSF file does not store the length of its tracks: without a limit they
loop forever. Each track now plays for a given time, fades out, then the
next track of the file starts (and the next file after the last track).
    defaultlength=150   seconds when the file gives no length (0 = forever)
    loops=2             loop count when the file gives intro + loop (VGM)
    fade=8              seconds of fade-out
When the file provides a length (NSFe, SPC and others), it is used.
The [gme] section can be added as is to the ocp.ini in %APPDATA%; otherwise
these defaults apply. These settings can also be changed inside OCP: in the
file browser, go to setup: and open gmeconfig.dev (arrows, Esc to save).

Tracker view for chiptunes (section [gme])
------------------------------------------
While an NSF (NES), HES (PC Engine), GBS (Game Boy), SPC (SNES), VGM / GYM
(Genesis, Master System) or S98 (PC-88/PC-98) file plays, the "pattern" view
(key t) shows the channels like a tracker (FamiTracker, MOD/S3M). It is
rebuilt from the sound chip registers, read 60 times per second (50 for PAL),
and follows exactly what is heard. The rows below the current one are filled
in advance.
 - The number/channel mapping is shown in the title of the view
   (1:P1 2:P2 3:Tri 4:Noi 5:DMC and so on).
 - NSF: one column per channel: Pulse 1, Pulse 2, Triangle, Noise, DPCM,
   then the expansion chips (VRC6, VRC7, FDS, MMC5, N163, Sunsoft 5B).
   Instrument = pulse duty cycle, noise mode, VRC7 patch; volume 0-F.
 - HES: 6 channels (W1-W4, M1-M2). Instrument = waveform number, in order
   of appearance (01, 02 and so on); volume 00-1F. Channels 5-6 in noise mode:
   "X-#". DDA mode (samples, drums): "***".
 - GBS: 4 channels (Sq1, Sq2, Wav, Noi). Instrument = duty cycle (squares),
   waveform number (Wav), short noise mode (01).
 - SPC: 8 voices (V1-V8). Instrument = sample number. The SNES only knows
   the playback speed of a sample, not its note: the pitch of each looping
   sample is measured once from the sound RAM. One-shot samples (drums,
   effects) show their number, "Sxx", instead of a note.
 - VGM / GYM: FM1-FM6 (YM2612; FM6 shows "***" when it plays samples), then
   Sq1-Sq3 and Noi (SN76489), or OL1-OL9 for the YM2413 (Master System FM).
   Instrument = FM patch, numbered in order of appearance. Muting a PSG
   column mutes the whole PSG (the emulator has a single PSG voice).
 - S98: the chips listed in the file: FM channels (YM2203, YM2608, YM2612,
   YM2151), SSG A-C (SGA-SGC), rhythm (Rhy: B bass drum, S snare, H hi-hat,
   C cymbal, T tom, R rim shot) and ADPCM (PCM). Channels that the song never
   uses are hidden.
 - One row = 6 frames (1/10 s) by default. Each cell shows the note (or the
   noise frequency "X-#", the DPCM sample "Sxx"), the instrument and the
   volume. "---" = the channel went silent. Notes come from the actual
   frequencies.
 - The columns share the full width of the screen, whatever the number of
   channels. When there is room, a 4th field shows that the frequency moves
   during the row: ^ up, v down, ~ vibrato (CP437 arrows and symbol on
   screen). A vibrato or portamento does not repeat the note. With more
   channels the cells get shorter: note, instrument and volume, then note
   and volume (one digit), then the note alone; beyond about 18 channels on
   80 columns, OCP falls back to its usual layouts (channels scroll).
   Tab / PgUp / PgDn select another layout; Home returns to this one.
 - Keys 1 to 9 and 0: mute/unmute a channel; q and s as usual. (These keys
   also work with the other GME formats (SPC, VGM and others), one key per emulator
   voice.)
    trackerview=on   off for the original display
    rowframes=6      frames per row (1 to 32); 3 = more detail, 12 = more compact
The other formats (KSS, AY, SAP and others) have no tracker view.

VGM files for other chips than the Sega ones (MSX, PC-98 and others)
--------------------------------------------------------------------
Game Music Emu only emulates the Sega chips (SN76489, YM2612, YM2413). VGM
and VGZ files that use other chips (AY-3-8910, Konami SCC, YM2151, YM2203,
YM2608, PCM chips and others) are now played by libvgm, like the S98 files,
and show the type "VGMX" in the file browser. Loops and fade follow the
[gme] section. The tracker view shows the AY-3-8910 / YM2149, Konami SCC
(5 channels, "SC1" to "SC5"), YM2151, YM2203, YM2608, YM2612, YM2413 and
SN76489 channels; PCM and OPL chips are not shown. VGM files for Sega
consoles keep using Game Music Emu with its tracker view, and OPL-only VGM
files keep using AdPlug. Files that a previous version tagged as OPL are
checked again automatically.

S98 files (NEC PC-88/PC-98, Sharp X1/X68000) - section [s98]
-----------------------------------------------------------
Played by the new plays98 player, based on libvgm (Valley Bell), which
emulates the Yamaha chips of these machines: YM2203 (OPN), YM2608 (OPNA)
with its SSG part, YM2151 (OPM), YM2612, OPLL/OPL and AY-3-8910/SN76489.
Title, artist and game are read from the file (Shift-JIS or UTF-8), the
length is shown in the file browser, the chips in the comment field.
The file gives its loop point: a looping track plays
    loops=2    times the looping part, then
    fade=8     seconds of fade-out.
A track without a loop ends by itself. With OCP's loop mode (Ctrl+L), the
track loops forever. Ctrl+Home restarts the track.

GSF files (Game Boy Advance) - played by mGBA
---------------------------------------------
GSF and MINIGSF files are played by the new playgsf player, which runs the
music program of the game in mGBA (Jeffrey Pfau), a Game Boy Advance
emulator. A MINIGSF file holds one track and needs its GSFLIB library (named
in the file), in the same directory or in the same ZIP archive; GSFLIB files
are not listed in the file browser.
Title, artist, game, year and length are read from the file. The track plays
for the length given by the file, then fades out for the given fade time;
without a length, the [gme] settings apply:
    defaultlength=150   seconds when the file gives no length (0 = forever)
    fade=8              seconds of fade-out when the file gives none
With OCP's loop mode (Ctrl+L), the track plays forever. Ctrl+Home restarts
the track. Key i shows the information view.
Tracker view: most games mix all their instruments by themselves into the
two digital channels of the console, so the notes cannot be read from the
sound chip. They are read from the sound engine of the game instead, when it
is MusicPlayer2000 (MP2K, also called "Sappy"), the engine of Nintendo's
SDK used by most GBA games: a second, silent mGBA runs the game a few
seconds ahead of the audio, as for the other tracker views. There is one
column per track of the song (for a chord, its newest and loudest note),
then the 4 Game Boy channels. Keys 1 to 0 mute the tracks, then the Game
Boy channels. The tracker view follows [gme] trackerview=.
A game that uses another sound engine has no tracker view; keys 1 to 6 then
mute the sound channels of the console: 1 to 4 are the Game Boy channels
(square 1, square 2, wave, noise), 5 and 6 the two digital channels (Direct
Sound A and B).

SID files (Commodore 64): end of the tune and PAL / NTSC (section [libsidplayfp])
---------------------------------------------------------------------------------
A SID tune plays forever. Omni ends it, in this order:
 - at its length in the song length database of the High Voltage SID
   Collection (HVSC): DOCUMENTS\Songlengths.md5 is found by itself when the
   file is inside a copy of the HVSC, wherever it is stored; otherwise give
   its path with songlengths=. The database knows a file by the MD5 of its
   content, so a copy whose title, author or release text was edited is not
   found;
 - when the tune starts again by itself after a silence of at least one
   second (loopdetect=on). The sound that follows the silence is compared
   with the start of the tune and with the previous pass, a few seconds
   ahead of what is heard: a silence inside the tune does not end it;
 - after defaultlength= seconds (300 by default, 0 = forever).
After a length, the tune fades out for fade= seconds (3 by default). With
OCP's loop mode (Ctrl+L), the tune plays forever.
Key R switches between PAL (50 Hz, Europe) and NTSC (60 Hz, America and
Japan) for the file being played, and starts the current tune again: a tune
written for the other standard plays too slow or too fast. The next files
use the setting of ocp.ini again (defaultC64=, forceC64=). While the other
standard is used, the Song Speed line of the information view (key t)
shows it in yellow.

Volume normalization (section [sound])
--------------------------------------
Every file is played at about the same loudness, whatever its format
(normalize=on, normalizetarget=-18 dBFS). The loudness is measured on the
sound itself, without the silences and the quiet parts.
 - A file never heard starts at its own volume. After 3 seconds of sound,
   its gain glides to the one measured in 1.5 seconds, then stays fixed
   until the end of the file.
 - When the file stops after at least 10 seconds of sound, its loudness is
   saved in CPLOUD.DAT, next to CPMODNFO.DAT in
   %APPDATA%\OpenCubicPlayer\Data; the next time, its gain is right from
   the start. The list grows as you listen.
 - The gain goes from -12 to +12 dB; a boost never takes the loudest sample
   measured above full scale. The files that hold several songs (NSF, SID
   and others) get the loudness of the first song heard.
Delete CPLOUD.DAT to measure everything again.

Audio streams of games (GENH, ADX, BRSTM and others) - played by vgmstream
---------------------------------------------------------------------------
The new playvgms player plays the recorded sound of games through vgmstream:
GENH, ADX (Sega), BRSTM (Wii), HCA, XA (PlayStation) and hundreds of other
formats, with the loop points of the files. A stream that loops plays the
looped part [gme] loops= times, then fades out for [gme] fade= seconds (the
same settings as the other game music formats). Files that hold several
streams (sound banks) have subsongs: keys < and > (or Ctrl+Left and
Ctrl+Right) go from one to the other. Key i shows the information view
(container, codec, channels, loop points).
vgmstream is built with its own decoders only (without FFmpeg, mpg123 or
Vorbis), so a few formats that need these libraries are not played. The
common formats (WAV, MP3, OGG, FLAC and others) stay with the players OCP
already has. As for any recorded sound, there is no tracker view.

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
Omni v10 (3.5.0-omni.10, 2026-11-28)
 - GSF and MINIGSF files (Game Boy Advance), played by mGBA
 - audio streams of games (GENH, ADX, BRSTM, HCA and hundreds of other
   formats), played by vgmstream
 - S98 and VGM files with a broken loop point no longer freeze OCP when
   they are opened
 - GSF: tracker view for the games that use the MP2K sound engine (one
   column per track of the song, then the Game Boy channels); keys 1 to 0
   mute the tracks
 - SID: end of the tune from the HVSC song length database, or when it
   starts again after a silence, or after defaultlength=; fade-out after a
   length
 - SID: key R switches between PAL (50 Hz) and NTSC (60 Hz) for the file
   being played
 - Volume normalization: every file at about the same loudness, whatever
   its format; the loudness of the files heard is kept in CPLOUD.DAT
 - Alt+K: one row per action, with all its keys ("H ? ! F1"); letters are
   shown in upper case, lower and upper case being the same key
 - Closing the window while the Alt+K list is shown no longer freezes OCP
 - SID: the Song Speed line shows in yellow the standard chosen with key R
 - ALT-C: setup of the players (SID, volume normalization, S98, game
   music), saved to ocp.ini with ALT-S
Omni v9 (3.5.0-omni.9, 2026-10-23)
 - Shuffle play: off / on (each file once) / endless (new rounds), also for
   the automatic advance in a directory; random order no longer the same at
   every start
 - VGM / VGZ files for MSX, PC-98, X68000 and others played by libvgm, with
   the tracker view (new: Konami SCC)
 - VGZ files are shown as plain files (no longer as a directory holding a
   .vgm): Enter plays them, and the automatic advance goes through them
 - S98 files in yellow in the file browser
 - ZIP and other archives are shown as folders to enter by default
   (putarchives=off; ALT-C item C, renamed "list archive contents inline")
 - macOS: "Quit Open Cubic Player" in the application menu (and Cmd+Q) quits
Omni v8 (3.5.0-omni.8, 2026-10-20)
 - Tracker view: the upcoming rows are filled in advance (a second, silent
   emulator runs a few seconds ahead of the audio)
 - Channel names in the column header of the tracker view, shorter title
 - 128x48 text mode by default (screentype=4)
 - Tracker view: the upcoming rows come back after a pause (with fade) or a
   speed change; they also work at another speed than 100 %
 - File browser status line: "F1 = help, ALT-K = shortcuts, ALT-C = setup"
 - Tracker view also for SPC (SNES), VGM / GYM (Genesis, Master System) and
   S98 (PC-88/PC-98); shorter cells when there are many channels
 - The volume set with - / + or F2 / F3 is kept for the next files
 - The file browser starts in your Music folder (the one Windows Explorer
   shows, even if moved to OneDrive or another drive); another start folder
   can be set in ALT-C (item F, saved with ALT-S to [fileselector] path=,
   "." = where OCP is started). TAB completes folder names there and in the
   path editor (ALT-Enter)
 - macOS build (Apple Silicon application bundle)
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
 - GME (NSF, GBS, KSS, VGM and others): default length, loops and fade ([gme])
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
   its own (MAME BSD/GPL, Nuked LGPL and others). Check before redistributing.
 - ocp.ini: screentype=4 (128x48), autonext, [gme] and [s98] sections
 - omni-version.h (new): Omni release number, shown in the title bar;
   filesel/mdb.c rescans files of unknown type after an Omni update
 - libgme (game-music-emu, separate repository): new API gme_vis_enable /
   gme_vis_read (NSF, HES, GBS, VGM, GYM, SPC) that copies the sound chip
   registers once per frame
 - plays98/s98vis.c (new): S98 tracker view, reads the command stream ahead
   of libvgm; plays98 shares playgme/nsftrak.c and nsfvis.c
 - playgsf/ (new): GSF player; mGBA 0.10.5 (Mozilla Public License 2.0),
   built with only its Game Boy Advance core, is linked statically into
   95-playgsf.dll. filesel/pfilesel.c: file extensions of up to 7
   characters (MINIGSF)
 - playgsf/gsfvis.c (new): tracker view from the state of the MP2K sound
   engine, read in a second mGBA; playgsf shares playgme/nsftrak.c and
   nsfvis.c
 - playsid/sidend.c, sidlength.c (new): end of the SID tunes (loop
   detection, HVSC song lengths); key R (PAL / NTSC) in sidpplay.cpp
 - cpiface/cpikeyhelp.c: Alt+K groups the keys of the same action
 - cpiface/cpinorm.c (new): volume normalization of the sound sent to the
   player device
 - playvgms/ (new): vgmstream player; vgmstream r2168 (ISC license), built
   without FFmpeg, mpg123 and Vorbis, is linked statically into
   95-playvgms.dll
iconv is provided by win-iconv instead of GNU libiconv.
