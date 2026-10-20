Open Cubic Player - Omni, macOS build
=====================================

A customized build of Open Cubic Player (https://github.com/mywave82/opencubicplayer).
See https://github.com/laurentlasalle/opencubicplayer for the changes and the sources.

Installation
------------
Copy "Open Cubic Player.app" to Applications (or anywhere else).

The application is not signed with an Apple Developer certificate, so macOS blocks
it the first time:
  1. Double-click it; macOS says it cannot verify the application. Click "Done".
  2. Open System Settings > Privacy & Security, scroll down and click "Open Anyway"
     next to the message about Open Cubic Player, then confirm.
If macOS says the application "is damaged", remove the download quarantine in
Terminal instead, then open it normally:
  xattr -dr com.apple.quarantine "/Applications/Open Cubic Player.app"

Settings and files
------------------
- Configuration: ~/.config/ocp/ocp.ini (created on the first start). If you have
  used another version of OCP before, delete or rename this file to get the
  Omni defaults (128x48 window, tracker view settings...).
- The file selector starts in your home folder; another start folder can be
  set in ALT-C (item F, TAB completes folder names, ALT-S saves).
- The window opens where it was when OCP was last closed (centered the first
  time).
- HiDPI (Retina) screens are drawn at full resolution. [screen] sdlscale= in
  ocp.ini forces the number of screen pixels per OCP pixel (0 = automatic).

Keyboard
--------
- Function keys: hold fn (or enable "Use F1, F2, etc. keys as standard function
  keys" in System Settings > Keyboard).
- Alt shortcuts use the Option key.
- Ctrl+Left/Right (previous/next track in files with several songs) are taken by
  macOS to switch desktops: use < and > instead, or turn off "Move left/right a
  space" in System Settings > Keyboard > Keyboard Shortcuts > Mission Control.
- F1 = help, Alt-K = list of the keys available on the current screen.

Command line
------------
The executable is "Open Cubic Player.app/Contents/MacOS/ocp". Started from the
bundle it opens the SDL3 window; pass -dcurses to use the terminal instead.
