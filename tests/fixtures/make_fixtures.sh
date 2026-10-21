#!/bin/sh
# Rebuilds the fixture media. Needs the ffmpeg command-line tool. Movies the
# tests compare exactly use lossless codecs (QuickTime Animation, PCM).
set -e
cd "$(dirname "$0")"
FF="ffmpeg -hide_banner -loglevel error -y"

# 30 frames at 15 fps; frame N is a solid colour (N*8, 255-N*8, 64), with a
# 440 Hz tone at 22.05 kHz.
$FF -f lavfi -i "color=c=black:s=160x120:r=15:d=2,format=rgb24,geq=r='mod(N*8\,256)':g='255-mod(N*8\,256)':b=64" \
    -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=2" \
    -c:v qtrle -pix_fmt rgb24 -c:a pcm_s16le -ac 1 -shortest counter.mov

# 15 frames at 15 fps with alpha: the left half opaque red, the right half
# transparent.
$FF -f lavfi -i "color=c=red:s=160x120:r=15:d=1,format=argb,geq=r='255':g='0':b='0':a='if(lt(X\,80)\,255\,0)'" \
    -c:v qtrle -pix_fmt argb alpha.mov

# 90 frames at 30 fps of H.264 with B-frames; frame N is grey level 2N.
# Lossy, so tests allow a tolerance; it exercises seeking and reordering.
$FF -f lavfi -i "color=c=black:s=320x240:r=30:d=3,format=gray,geq=lum='2*N'" \
    -c:v libx264 -pix_fmt yuv420p -g 30 -bf 2 -preset veryfast -crf 12 h264.mp4

# A 200x100 still: blue on the left, yellow on the right.
$FF -f lavfi -i "color=c=blue:s=100x100:d=1,format=rgb24" \
    -f lavfi -i "color=c=yellow:s=100x100:d=1,format=rgb24" \
    -filter_complex "[0][1]hstack" -frames:v 1 still.png

# One second of 660 Hz, sound only.
$FF -f lavfi -i "sine=frequency=660:sample_rate=22050:duration=1" -c:a pcm_s16le tone.wav
