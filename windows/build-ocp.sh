#!/usr/bin/env bash
# Cross-compiles OCP for Windows x64 and installs it into $INSTALL.
# Usage: build-ocp.sh [nocfg]     (nocfg = skip ./configure)
# Host tools needed by the OCP build: xa (xa65) and hexdump.
set -e
. "$(dirname "$0")/config.sh"
B=$(builtins)
cd $OCP_SRC
git submodule update --init --recursive
if test "$1" != "nocfg"; then
./configure \
  --host $HOST \
  CC=$HOST-clang CXX=$HOST-clang++ \
  CFLAGS="-I$PREFIX/include/ -O2" \
  CXXFLAGS="-I$PREFIX/include/ -O2" \
  LDFLAGS="-L$PREFIX/lib/ -Wl,$B" \
  --prefix="$INSTALL" --libdir="$INSTALL" --docdir="$INSTALL" --bindir="$INSTALL" \
  --sysconfdir="$INSTALL" --datadir="$INSTALL" --datarootdir="$INSTALL" --mandir="$INSTALL" \
  --without-info --without-update-mime-database --without-desktop_file_install \
  --without-update-desktop-database --with-unifont-relative > $WORK/ocp-configure.log 2>&1 || { tail -40 $WORK/ocp-configure.log; exit 1; }
fi
make -j$J all > $WORK/ocp-make.log 2>&1 || { grep -n -B2 -A6 "error" $WORK/ocp-make.log | tail -60; exit 1; }
rm -rf $INSTALL
make install > $WORK/ocp-install.log 2>&1 || { tail -30 $WORK/ocp-install.log; exit 1; }
echo "OCP installed in $INSTALL"
