#!/bin/sh
set -eu

cd "$(dirname "$0")"

cc=${CC:-cc}
cflags=${CFLAGS:--O2 -g}
extra_flags=
if [ "${SANITIZE:-0}" = "1" ]; then
    extra_flags="-O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
fi

binary="${TMPDIR:-/tmp}/zbitxv2-test-ft8-receive-$$"
trap 'rm -f "$binary"' EXIT HUP INT TERM

$cc -D_GNU_SOURCE -I../ft8_lib $cflags $extra_flags -Wall -Wextra -std=c11 \
    -o "$binary" \
    test_ft8_receive_diagnostics.c \
    ../ft8_lib/ft8/constants.c \
    ../ft8_lib/ft8/crc.c \
    ../ft8_lib/ft8/decode.c \
    ../ft8_lib/ft8/ldpc.c \
    ../ft8_lib/ft8/message.c \
    ../ft8_lib/ft8/text.c \
    ../ft8_lib/fft/kiss_fft.c \
    ../ft8_lib/fft/kiss_fftr.c \
    -lm

"$binary"
