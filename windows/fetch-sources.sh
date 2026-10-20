#!/usr/bin/env bash
# Downloads the toolchain and the pinned sources of all dependencies into $WORK.
set -e
. "$(dirname "$0")/config.sh"
mkdir -p $SRC $WORK/tc

dl () { # url file
	test -f "$SRC/$2" || curl -fsSL -o "$SRC/$2" "$1"
}
clone () { # repo ref dir
	test -d "$SRC/$3" || git clone -q --depth 1 --branch "$2" "$1" "$SRC/$3"
}
clone_commit () { # repo commit dir (branches without tags)
	if ! test -d "$SRC/$3"; then
		git init -q "$SRC/$3"
		git -C "$SRC/$3" fetch -q --depth 1 "$1" "$2"
		git -C "$SRC/$3" checkout -q FETCH_HEAD
	fi
}

if ! test -d $TC; then
	curl -fsSL -o $WORK/tc/$LLVM_MINGW.tar.xz https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_TAG/$LLVM_MINGW.tar.xz
	tar xf $WORK/tc/$LLVM_MINGW.tar.xz -C $WORK/tc
fi

clone https://github.com/madler/zlib v1.3.1 zlib
clone_commit https://github.com/libarchive/bzip2 1ea1ac188ad4b9cb662e3f8314673c63df95a589 bzip2
clone_commit https://github.com/markjeee/libmad c2f96fa4166446ac99449bdf6905f4218fb7d6b5 libmad
dl https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz libjpeg-turbo-3.2.0.tar.gz
clone https://github.com/pnggroup/libpng v1.6.58 libpng
dl https://github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.gz libogg-1.3.6.tar.gz
dl https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.gz libvorbis-1.3.7.tar.gz
dl https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz flac-1.5.0.tar.xz
clone https://github.com/xiph/opus v1.6.1 opus
clone https://github.com/xiph/speex Speex-1.2.1 speex
dl https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-devel-3.4.12-mingw.tar.gz SDL3-devel-3.4.12-mingw.tar.gz
dl https://github.com/harfbuzz/harfbuzz/releases/download/14.3.0/harfbuzz-14.3.0.tar.xz harfbuzz-14.3.0.tar.xz
clone https://github.com/freetype/freetype VER-2-14-3 freetype
dl https://github.com/metabrainz/libdiscid/releases/download/v0.7.0/libdiscid-0.7.0-win.zip libdiscid-0.7.0-win.zip
clone https://github.com/DaveGamble/cJSON v1.7.18 cJSON
clone https://github.com/temisu/ancient v2.3.0 ancient
clone_commit https://github.com/autoconf-archive/autoconf-archive 92c33ce5c3eeabc7c60ca92b1a6e813bb6a5ca05 autoconf-archive
clone_commit https://github.com/win-iconv/win-iconv 70a279dcfe6318bbe63e019e33b3230b55c19762 win-iconv
dl https://github.com/dbry/WavPack/releases/download/5.9.0/wavpack-5.9.0.tar.xz wavpack-5.9.0.tar.xz
clone_commit https://github.com/ValleyBell/libvgm a6eed945ecf4e5a25cf82004779c6f7e4fd5c319 libvgm
clone "$GME_REPO" "$GME_REF" game-music-emu
echo "sources ready in $SRC"
