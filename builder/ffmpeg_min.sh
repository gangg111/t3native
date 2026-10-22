#!/bin/bash
# The builder's FFmpeg: LGPL (no --enable-gpl), shared libraries, only what the remake's converter
# (tools/remake_import: media.py, disc_image.py) and t3native's movies_from_disc.py ask of it:
#   - XA music and STR movies: demuxer psxstr, decoders adpcm_xa and mdec, frames out as PNG
#     (image2), sound as WAV; music to Ogg Vorbis (libvorbis). libtheora only so the converter's
#     FFmpeg check (media.FFmpeg.ogg: Theora and Vorbis encoders) picks Ogg output; the builder
#     encodes the movies to MPEG-1 itself.
#   - movies: PNG frames + WAV to MPEG-1 video + MP2 in an MPEG program stream at 44.1 kHz
#     (scale, aresample), checked by decoding them again (ffprobe -count_frames, -f null). The
#     program stream demuxer names its video stream's codec by probing it with the raw
#     `mpegvideo` demuxer: without that one the stream reads as "unknown codec", no frames.
#     `-f null` encodes the decoded video with `wrapped_avframe` (its default).
#   - compressed audio tracks of a cue sheet (disc_image.COMPRESSED_SUFFIXES: .ape .flac .wav) to
#     raw s16le PCM.
# Run in MSYS2's MINGW64 shell from the unpacked source (a path without spaces):
#   bash ffmpeg_min.sh <source> <prefix>
#   bash ffmpeg_min.sh <source> <prefix> collect    (only the last step: DLLs, strip, licences)
set -euo pipefail
SRC=$1
PREFIX=$2

# The DLLs from MSYS2 the build links (libvorbis, libogg, libtheora, libwinpthread, ...) next to
# the programs, everything stripped, and the licences of FFmpeg and of each library beside them.
collect() {
  local src=$1 prefix=$2
  cd "$prefix/bin"
  for dll in $(ldd ffmpeg.exe ffprobe.exe ./*.dll | awk '$3 ~ /^\/mingw64\// {print $3}' | sort -u); do
    cp -u "$dll" .
  done
  strip ./*.exe ./*.dll
  mkdir -p licenses
  cp "$src/COPYING.LGPLv2.1" "$src/LICENSE.md" licenses/
  for pkg in libogg zlib winpthreads; do
    for f in /mingw64/share/licenses/$pkg/*; do [ -e "$f" ] && cp "$f" "licenses/$pkg-$(basename "$f")"; done
  done
  # MSYS2's libvorbis and libtheora packages carry no licence file; both are Xiph.Org's, under
  # the same BSD licence as libogg.
  printf '%s\n' "libvorbis and libtheora: Copyright (c) Xiph.Org Foundation. Distributed under the" \
    "same BSD-style licence as libogg (libogg-COPYING), with Xiph.Org Foundation as the copyright holder." \
    > licenses/libvorbis-libtheora-LICENSE.txt
  ls -la
}

if [ "${3:-}" = collect ]; then
  export PATH=/mingw64/bin:/usr/bin:$PATH
  collect "$SRC" "$PREFIX"
  exit 0
fi

export MSYSTEM=MINGW64
export PATH=/mingw64/bin:/usr/bin:$PATH
export TMPDIR=/tmp TMP=/tmp TEMP=/tmp
cd "$SRC"
./configure --prefix="$PREFIX" \
  --enable-shared --disable-static --enable-small \
  --disable-everything --disable-autodetect --disable-network --disable-doc --disable-debug \
  --disable-ffplay --disable-avdevice \
  --enable-libvorbis --enable-libtheora --enable-zlib \
  --enable-encoder=libvorbis,libtheora,pcm_s16le,png,mpeg1video,mp2,wrapped_avframe \
  --enable-decoder=mdec,adpcm_xa,png,pcm_s16le,mpeg1video,mp2,ape,flac,vorbis \
  --enable-demuxer=str,wav,image2,mpegps,mpegvideo,mp3,ape,flac,ogg \
  --enable-muxer=wav,image2,ogg,mpeg1system,pcm_s16le,null \
  --enable-parser=mpegvideo,mpegaudio,png,flac,vorbis \
  --enable-filter=null,anull,format,aformat,scale,aresample,setpts,asetpts \
  --enable-protocol=file,pipe
make -j8
make install
collect "$SRC" "$PREFIX"
