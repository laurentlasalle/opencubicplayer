#!/usr/bin/env bash
# Builds Open Cubic Player (Omni) for macOS and packages it as an application bundle.
# Run it on a Mac with Homebrew (Apple Silicon or Intel); everything is built below $WORK.
#
#   macos/build-macos.sh            all steps
#   VERSION_SUFFIX=-test1 macos/build-macos.sh
#
# Result: $WORK/dist/ocp-<version>-macos-<arch>.zip, containing "Open Cubic Player.app"
# and README-macOS.txt. The bundle is self-contained (Homebrew libraries are copied into
# Contents/Frameworks) and ad-hoc signed.
set -e
OCP_SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=${WORK:-$HOME/ocp-mac-work}
SRC=$WORK/src
PREFIX=$WORK/prefix          # libraries built from source (GME fork, libvgm)
BUILD=$WORK/build
DIST=$WORK/dist
APP=$WORK/app/Open\ Cubic\ Player.app
J=${J:-$(sysctl -n hw.ncpu)}
ARCH=$(uname -m)
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-14.0}

# same pinned sources as the Windows build
GME_REPO=$(sed -n 's/^GME_REPO=\${GME_REPO:-\([^}]*\)}.*/\1/p' $OCP_SRC/windows/config.sh)
GME_REF=$(sed -n 's/^GME_REF=\${GME_REF:-\([0-9a-f]*\)}.*/\1/p' $OCP_SRC/windows/config.sh)
LIBVGM_REF=$(sed -n 's/^clone_commit https:\/\/github.com\/ValleyBell\/libvgm \([0-9a-f]*\) .*/\1/p' $OCP_SRC/windows/fetch-sources.sh)

# <upstream OCP version>-omni.<Omni release>, e.g. 3.5.0-omni.8
VERSION=$(sed -n 's/^PACKAGE_VERSION=.\([0-9.]*\).$/\1/p' $OCP_SRC/configure)-omni.$(sed -n 's/^#define OMNI_VERSION \([0-9]*\)$/\1/p' $OCP_SRC/omni-version.h)$VERSION_SUFFIX

step () { echo; echo "=== $* ==="; }

step Homebrew packages
BREW_PACKAGES="pkgconf xa autoconf automake libtool cmake sdl3 mad libogg libvorbis flac opus speex
	jpeg-turbo libpng freetype cjson libdiscid ancient wavpack"
brew install --quiet $BREW_PACKAGES
BREW=$(brew --prefix)

clone_commit () { # repo commit dir
	if ! test -d "$SRC/$3"; then
		git init -q "$SRC/$3"
		git -C "$SRC/$3" fetch -q --depth 1 "$1" "$2"
		git -C "$SRC/$3" checkout -q FETCH_HEAD
	fi
}
mkdir -p $SRC $BUILD $DIST
clone_commit "$GME_REPO" "$GME_REF" game-music-emu
clone_commit https://github.com/ValleyBell/libvgm "$LIBVGM_REF" libvgm

step "game-music-emu (fork with the visualisation API)"
if ! test -f $PREFIX/.gme-$GME_REF; then
	rm -rf $BUILD/gme
	cmake -S $SRC/game-music-emu -B $BUILD/gme -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_INSTALL_NAME_DIR=$PREFIX/lib \
		-DBUILD_SHARED_LIBS=ON -DENABLE_UBSAN=OFF -DGME_BUILD_TESTING=OFF -DGME_BUILD_EXAMPLES=OFF \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 > $BUILD/gme.log 2>&1 || { tail -40 $BUILD/gme.log; exit 1; }
	make -C $BUILD/gme -j$J install >> $BUILD/gme.log 2>&1 || { tail -60 $BUILD/gme.log; exit 1; }
	touch $PREFIX/.gme-$GME_REF
fi

step "libvgm (static, linked into the S98 plugin)"
if ! test -f $PREFIX/.libvgm-$LIBVGM_REF; then
	rm -rf $BUILD/libvgm
	cmake -S $SRC/libvgm -B $BUILD/libvgm -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX \
		-DBUILD_LIBAUDIO=OFF -DBUILD_PLAYER=OFF -DBUILD_VGM2WAV=OFF -DUSE_SANITIZERS=OFF \
		-DBUILD_SHARED_LIBS=OFF -DLINK_STATIC_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 > $BUILD/libvgm.log 2>&1 || { tail -40 $BUILD/libvgm.log; exit 1; }
	make -C $BUILD/libvgm -j$J install >> $BUILD/libvgm.log 2>&1 || { tail -60 $BUILD/libvgm.log; exit 1; }
	touch $PREFIX/.libvgm-$LIBVGM_REF
fi

step "Open Cubic Player"
# Installed flat into Contents/Resources (the loader looks there first, see boot/kickload-unix.c);
# the executable is then moved to Contents/MacOS.
RES="$APP/Contents/Resources"
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$BREW/lib/pkgconfig
export PATH=$BREW/bin:$PATH
cd $OCP_SRC
git submodule update --init --recursive
make distclean > /dev/null 2>&1 || true
./configure \
	CFLAGS="-I$PREFIX/include -I$BREW/include -O2" \
	CXXFLAGS="-I$PREFIX/include -I$BREW/include -O2" \
	CPPFLAGS="-I$PREFIX/include -I$BREW/include" \
	LDFLAGS="-L$PREFIX/lib -L$BREW/lib -Wl,-headerpad_max_install_names" \
	LIBDIROCP="$RES" DATADIROCP="$RES" \
	--prefix="$RES" --bindir="$RES" --libdir="$RES" --datadir="$RES" --datarootdir="$RES" \
	--sysconfdir="$RES" --docdir="$RES/doc" --mandir="$RES/man" \
	--without-x11 --without-info --without-update-mime-database --without-desktop_file_install \
	--without-update-desktop-database --with-unifont-relative > $BUILD/ocp-configure.log 2>&1 \
	|| { tail -40 $BUILD/ocp-configure.log; exit 1; }
grep -E "^(  |)(SDL|libgme|game-music|S98|vgm|coreaudio|CoreAudio)" -i $BUILD/ocp-configure.log | head -20 || true
make -j$J all > $BUILD/ocp-make.log 2>&1 || { grep -n -B2 -A6 "error" $BUILD/ocp-make.log | tail -60; exit 1; }
rm -rf "$WORK/app"
make install > $BUILD/ocp-install.log 2>&1 || { tail -30 $BUILD/ocp-install.log; exit 1; }

step "Application bundle"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks"
mv "$RES/ocp" "$APP/Contents/MacOS/ocp"
rm -rf "$RES"/ocp-* "$RES/icons" "$RES/mime" "$RES/applications" "$RES/man" "$RES/info"

# icon
ICONSET=$BUILD/ocp.iconset
rm -rf $ICONSET; mkdir -p $ICONSET
cp desktop/opencubicplayer-16x16.png   $ICONSET/icon_16x16.png
cp desktop/opencubicplayer-32x32.png   $ICONSET/icon_16x16@2x.png
cp desktop/opencubicplayer-32x32.png   $ICONSET/icon_32x32.png
cp desktop/opencubicplayer-64x64.png   $ICONSET/icon_32x32@2x.png
cp desktop/opencubicplayer-128x128.png $ICONSET/icon_128x128.png
iconutil -c icns -o "$RES/ocp.icns" $ICONSET || echo "(no icon)"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>         <string>ocp</string>
	<key>CFBundleIdentifier</key>         <string>io.github.laurentlasalle.opencubicplayer</string>
	<key>CFBundleName</key>               <string>Open Cubic Player</string>
	<key>CFBundleDisplayName</key>        <string>Open Cubic Player</string>
	<key>CFBundleIconFile</key>           <string>ocp.icns</string>
	<key>CFBundlePackageType</key>        <string>APPL</string>
	<key>CFBundleShortVersionString</key> <string>$VERSION</string>
	<key>CFBundleVersion</key>            <string>$VERSION</string>
	<key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
	<key>LSMinimumSystemVersion</key>     <string>$MACOSX_DEPLOYMENT_TARGET</string>
	<key>NSHighResolutionCapable</key>    <true/>
	<key>LSApplicationCategoryType</key>  <string>public.app-category.music</string>
</dict>
</plist>
EOF

# Copy the non-system libraries into Contents/Frameworks and point every reference there.
python3 - "$APP" "$PREFIX/lib" "$BREW/lib" <<'PY'
import os, shutil, subprocess, sys
app, search = sys.argv[1], sys.argv[2:]
fw = os.path.join(app, 'Contents', 'Frameworks')
def macho(p):
    with open(p, 'rb') as f:
        return f.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xce\xfa\xed\xfe')
def deps(p):   # dependencies, without the library's own id
    out = subprocess.run(['otool', '-L', p], capture_output=True, text=True, check=True).stdout
    own = subprocess.run(['otool', '-D', p], capture_output=True, text=True).stdout.splitlines()[1:]
    return [r for r in (l.strip().split(' (')[0] for l in out.splitlines()[1:] if l.strip()) if r not in own]
def rpaths(p):
    out = subprocess.run(['otool', '-l', p], capture_output=True, text=True).stdout.splitlines()
    return [out[i + 2].split()[1] for i, l in enumerate(out) if 'LC_RPATH' in l]
def resolve(ref, origin):
    if ref.startswith('@loader_path/'):
        c = os.path.join(os.path.dirname(origin), ref[13:])
        return c if os.path.exists(c) else None
    if ref.startswith('@rpath/'):
        for d in [r.replace('@loader_path', os.path.dirname(origin)) for r in rpaths(origin)] + search:
            c = os.path.join(d, ref[7:])
            if os.path.exists(c):
                return c
        return None
    return ref if os.path.exists(ref) else None
todo = [(p, p) for dp, _, fs in os.walk(app) for p in [os.path.join(dp, f) for f in fs]
        if not os.path.islink(p) and macho(p)]   # (file in the bundle, where it came from)
copied = {}
while todo:
    p, origin = todo.pop()
    for ref in deps(p):
        if ref.startswith(('/usr/lib/', '/System/')) or ref.startswith('@executable_path/'):
            continue
        name = os.path.basename(ref)
        src = resolve(ref, origin)
        if src is None:
            sys.exit('cannot find %s (needed by %s)' % (ref, p))
        dst = os.path.join(fw, name)
        if name not in copied:
            shutil.copy(os.path.realpath(src), dst)
            os.chmod(dst, 0o755)
            subprocess.run(['install_name_tool', '-id', '@executable_path/../Frameworks/' + name, dst], check=True)
            copied[name] = src
            todo.append((dst, os.path.realpath(src)))
        subprocess.run(['install_name_tool', '-change', ref, '@executable_path/../Frameworks/' + name, p], check=True)
print('bundled:', ' '.join(sorted(copied)))
PY

# every Mach-O file must carry a signature on Apple Silicon (install_name_tool broke them)
find "$APP" -type f \( -name "*.dylib" -o -perm -u+x \) | while read -r f; do
	if file "$f" | grep -q Mach-O; then codesign --force --sign - "$f" 2> /dev/null; fi
done
codesign --force --sign - "$APP"
codesign --verify --verbose "$APP" || true

step Package
PKG=ocp-$VERSION-macos-$ARCH
rm -rf "$WORK/pkg"; mkdir -p "$WORK/pkg/$PKG"
ditto "$APP" "$WORK/pkg/$PKG/Open Cubic Player.app"
cp $OCP_SRC/macos/README-macOS.txt "$WORK/pkg/$PKG/"
rm -f $DIST/$PKG.zip
( cd "$WORK/pkg" && ditto -c -k --keepParent "$PKG" $DIST/$PKG.zip )
echo "$DIST/$PKG.zip"
