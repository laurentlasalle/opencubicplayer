#!/usr/bin/env bash
# Cross-compiles the dependencies of OCP for Windows x64 with llvm-mingw.
# Run fetch-sources.sh first. A step is skipped when $PREFIX/.done-<name> exists.
set -e
. "$(dirname "$0")/config.sh"
B=$BUILD
mkdir -p $PREFIX/lib/pkgconfig $PREFIX/include $PREFIX/bin $B

cat > $B/toolchain.cmake <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER $TC/bin/$HOST-clang)
set(CMAKE_CXX_COMPILER $TC/bin/$HOST-clang++)
set(CMAKE_RC_COMPILER $TC/bin/$HOST-windres)
set(CMAKE_FIND_ROOT_PATH $PREFIX $TC/$HOST)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

do_cmake () { # $1 = source dir, rest = args
  local s=$1; shift
  local b=$B/$(basename $s)-build
  rm -rf $b; mkdir -p $b
  cmake -S $s -B $b -G"Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=$B/toolchain.cmake \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_PREFIX_PATH=$PREFIX \
    -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    "$@" > $b.log 2>&1 || { tail -40 $b.log; return 1; }
  make -C $b -j$J install >> $b.log 2>&1 || { tail -60 $b.log; return 1; }
}
done_mark () { touch $PREFIX/.done-$1; }
is_done () { test -f $PREFIX/.done-$1; }
unpack () { rm -rf $B/$2; mkdir -p $B/$2; tar xf $SRC/$1 -C $B/$2 --strip-components=1; }

step () { echo "=== $1"; }

# zlib
if ! is_done zlib; then step zlib
  do_cmake $SRC/zlib -DZLIB_BUILD_EXAMPLES=OFF
  # provide zlib1.dll name expectations
  done_mark zlib; fi

# bzip2 (only the library)
if ! is_done bzip2; then step bzip2
  rm -rf $B/bzip2; cp -r $SRC/bzip2 $B/bzip2; cd $B/bzip2; echo "#define BZ_VERSION \"1.1.0\"" > bz_version.h
  for f in blocksort huffman crctable randtable compress decompress bzlib; do
    $HOST-clang -O2 -DWIN32 -D_FILE_OFFSET_BITS=64 -c $f.c -o $f.o; done
  $HOST-clang -shared -o bz2.dll -Wl,--out-implib,libbz2.dll.a *.o
  cp bz2.dll $PREFIX/bin; cp libbz2.dll.a $PREFIX/lib; cp bzlib.h $PREFIX/include
  cd $WORK; done_mark bzip2; fi

# libmad
if ! is_done mad; then step mad
  rm -rf $B/libmad; cp -r $SRC/libmad $B/libmad; cd $B/libmad
  sed -i 's/optimize="\$optimize -fforce-mem"/:/' configure.ac configure
  touch AUTHORS ChangeLog NEWS
  autoreconf -fi > ../mad.log 2>&1
  ./configure --host $HOST --prefix=$PREFIX --disable-static >> ../mad.log 2>&1
  make -j$J CFLAGS="-O2 -DFPM_64BIT" LDFLAGS=-no-undefined install >> ../mad.log 2>&1 || { tail -40 ../mad.log; exit 1; }
  cd $WORK; done_mark mad; fi

if ! is_done jpeg; then step jpeg
  unpack libjpeg-turbo-3.2.0.tar.gz jpeg; do_cmake $B/jpeg -DENABLE_SHARED=TRUE -DENABLE_STATIC=FALSE; done_mark jpeg; fi

if ! is_done png; then step png
  do_cmake $SRC/libpng -DPNG_STATIC=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DZLIB_ROOT=$PREFIX; done_mark png; fi

if ! is_done ogg; then step ogg
  unpack libogg-1.3.6.tar.gz ogg; do_cmake $B/ogg; done_mark ogg; fi

if ! is_done vorbis; then step vorbis
  unpack libvorbis-1.3.7.tar.gz vorbis
  sed -i 's/^LIBRARY$/LIBRARY vorbis/' $B/vorbis/win32/vorbis.def
  sed -i 's/^LIBRARY$/LIBRARY vorbisenc/' $B/vorbis/win32/vorbisenc.def
  sed -i 's/^LIBRARY$/LIBRARY vorbisfile/' $B/vorbis/win32/vorbisfile.def
  sed -i 's/cmake_minimum_required(VERSION 2.8.12)/cmake_minimum_required(VERSION 3.6)/' $B/vorbis/CMakeLists.txt
  do_cmake $B/vorbis
  # the import libraries reference ogg.dll / vorbis*.dll (LIBRARY lines of the .def files)
  for n in ogg vorbis vorbisenc vorbisfile; do cp $PREFIX/bin/lib$n.dll $PREFIX/bin/$n.dll; done
  done_mark vorbis; fi

if ! is_done flac; then step flac
  unpack flac-1.5.0.tar.xz flac
  do_cmake $B/flac -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF -DWITH_STACK_PROTECTOR=OFF
  done_mark flac; fi

if ! is_done opus; then step opus
  do_cmake $SRC/opus -DOPUS_DEEP_PLC=OFF -DOPUS_DRED=OFF -DOPUS_OSCE=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF
  done_mark opus; fi

if ! is_done speex; then step speex
  rm -rf $B/speex; cp -r $SRC/speex $B/speex; cd $B/speex
  (./autogen.sh && ./configure --host $HOST --prefix=$PREFIX --disable-static --disable-binaries && make -j$J install) > ../speex.log 2>&1 || { tail -40 ../speex.log; exit 1; }
  cd $WORK; done_mark speex; fi

if ! is_done sdl3; then step sdl3
  rm -rf $B/sdl3; mkdir -p $B/sdl3; tar xf $SRC/SDL3-devel-3.4.12-mingw.tar.gz -C $B/sdl3
  cp -R $B/sdl3/SDL3-3.4.12/$HOST/* $PREFIX/
  sed -i "s#^prefix=.*#prefix=$PREFIX#" $PREFIX/lib/pkgconfig/sdl3.pc
  done_mark sdl3; fi

if ! is_done harfbuzz; then step harfbuzz
  unpack harfbuzz-14.3.0.tar.xz harfbuzz
  do_cmake $B/harfbuzz -DHB_HAVE_FREETYPE=OFF -DHB_BUILD_SUBSET=OFF -DHB_BUILD_UTILS=OFF -DHB_BUILD_TESTS=OFF
  done_mark harfbuzz; fi

if ! is_done freetype; then step freetype
  do_cmake $SRC/freetype -DFT_DISABLE_BZIP2=TRUE -DFT_REQUIRE_ZLIB=TRUE -DFT_REQUIRE_PNG=TRUE -DFT_REQUIRE_HARFBUZZ=TRUE -DFT_DISABLE_BROTLI=TRUE -DZLIB_ROOT=$PREFIX
  done_mark freetype; fi

if ! is_done discid; then step discid
  rm -rf $B/discid; mkdir -p $B/discid; unzip -q -o $SRC/libdiscid-0.7.0-win.zip -d $B/discid
  d=$B/discid/libdiscid-0.7.0-win
  cp -r $d/x64/* $PREFIX/lib; cp $d/x64/*.dll $PREFIX/bin 2>/dev/null || true
  cp -r $d/include/discid $PREFIX/include
  cat > $PREFIX/lib/pkgconfig/libdiscid.pc <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: libdiscid
Description: The MusicBrainz DiscID Library.
Version: 0.7.0
Libs: -L\${libdir} -ldiscid
Cflags: -I\${includedir}
EOF
  done_mark discid; fi

if ! is_done cjson; then step cjson
  rm -rf $B/cJSON; cp -r $SRC/cJSON $B/cJSON
  sed -i 's/cmake_minimum_required(VERSION 3.0)/cmake_minimum_required(VERSION 3.5)/; /-std=c89/d; /-Werror/d' $B/cJSON/CMakeLists.txt
  do_cmake $B/cJSON -DENABLE_CJSON_TEST=OFF -DENABLE_CUSTOM_COMPILER_FLAGS=OFF
  done_mark cjson; fi

if ! is_done ancient; then step ancient
  rm -rf $B/ancient; cp -r $SRC/ancient $B/ancient; cd $B/ancient
  mkdir -p m4; for m in ax_cxx_compile_stdcxx ax_check_compile_flag ax_cflags_warn_all ax_compiler_vendor ax_prepend_flag ax_require_defined; do cp $SRC/autoconf-archive/m4/$m.m4 m4/; done
  (aclocal -I m4 && autoreconf -fi && ./configure --host $HOST --prefix=$PREFIX --disable-static && make -j$J LDFLAGS=-Wl,$(builtins) install) > ../ancient.log 2>&1 || { tail -40 ../ancient.log; exit 1; }
  cd $WORK; done_mark ancient; fi

if ! is_done iconv; then step iconv
  rm -rf $B/win-iconv; cp -r $SRC/win-iconv $B/win-iconv; cd $B/win-iconv
  $HOST-clang -O2 -shared -o libiconv-2.dll win_iconv.c -Wl,--export-all-symbols -Wl,--out-implib,libiconv.dll.a
  cp libiconv-2.dll $PREFIX/bin; cp libiconv.dll.a $PREFIX/lib; cp iconv.h localcharset.h $PREFIX/include
  cd $WORK; done_mark iconv; fi

if ! is_done gme; then step gme
  do_cmake $SRC/game-music-emu -DENABLE_UBSAN=OFF -DGME_BUILD_TESTING=OFF -DGME_BUILD_EXAMPLES=OFF
  done_mark gme; fi

if ! is_done wavpack; then step wavpack
  unpack wavpack-5.9.0.tar.xz wavpack; cd $B/wavpack
  (./configure --host $HOST --prefix=$PREFIX --disable-static --enable-legacy --disable-apps && make -j$J install) > ../wavpack.log 2>&1 || { tail -40 ../wavpack.log; exit 1; }
  cd $WORK; done_mark wavpack; fi


# zlib: OCP links with -lz and looks for zlib.pc in lib/pkgconfig
if ! is_done zlibfix; then step zlib-names
  cp $PREFIX/lib/libzlib.dll.a $PREFIX/lib/libz.dll.a
  cp $PREFIX/share/pkgconfig/zlib.pc $PREFIX/lib/pkgconfig/
  sed -i 's/-lz$/-lzlib/; s/-lz /-lzlib /' $PREFIX/lib/pkgconfig/zlib.pc
  done_mark zlibfix; fi

# libvgm (static, linked into 95-plays98.dll)
if ! is_done vgm; then step libvgm
  b=$B/libvgm-build; rm -rf $b
  cmake -S $SRC/libvgm -B $b -G"Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$B/toolchain.cmake \
    -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_LIBAUDIO=OFF -DBUILD_PLAYER=OFF -DBUILD_VGM2WAV=OFF -DUSE_SANITIZERS=OFF \
    -DBUILD_SHARED_LIBS=OFF -DLINK_STATIC_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DZLIB_ROOT=$PREFIX > $b.log 2>&1 || { tail -40 $b.log; exit 1; }
  make -C $b -j$J install >> $b.log 2>&1 || { tail -60 $b.log; exit 1; }
  done_mark vgm; fi

echo "all dependencies built in $PREFIX"
