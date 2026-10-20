# Shared settings for the Windows cross-build scripts. Source this file.
# Everything is built below $WORK (override it in the environment).
WORK=${WORK:-$HOME/ocp-win-work}
OCP_SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

LLVM_MINGW_TAG=20261006
LLVM_MINGW=llvm-mingw-$LLVM_MINGW_TAG-ucrt-ubuntu-22.04-x86_64

# game-music-emu with the OCP visualisation API (gme_vis_enable / gme_vis_read)
GME_REPO=${GME_REPO:-https://github.com/laurentlasalle/game-music-emu}
GME_REF=${GME_REF:-5167786641bfa781e73839814525da5d5d640bb3}   # commit of the "ocp" branch

SRC=$WORK/src
TC=$WORK/tc/$LLVM_MINGW
PREFIX=$WORK/prefix
BUILD=$WORK/build
INSTALL=$WORK/ocp-win64
DIST=$WORK/dist
HOST=x86_64-w64-mingw32

export PATH=$TC/bin:$PATH
export PKG_CONFIG=pkg-config
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
J=${J:-$(nproc)}
builtins () { ls $TC/lib/clang/*/lib/windows/libclang_rt.builtins-x86_64.a; }
