#!/usr/bin/env bash
# build-hs.sh <Prog.hs> [out.elf] - compile a Haskell program with the
# riscv32 cross GHC and link it bare-metal against the bmkernel + the
# (glibc-built) RTS/base/ghc-prim/ghc-bignum. The RTS reaches the OS through
# glibc syscalls (ecall) which the bmkernel services; -V0 + no-sig-handlers +
# non-threaded RTS switch off timers/signals/threads.
# LAMBDA = the cross-toolchain installation (see README, Toolchain).
# BMOUT = where objects and the ELF land.
READELF="${READELF:-$(command -v riscv32-oe-linux-readelf || echo /opt/riscv/bin/riscv32-unknown-elf-readelf)}"
BMOUT="${BMOUT:-./bmout}"; mkdir -p "$BMOUT"
BMARKS="${BMARKS:-$(cd "$(dirname "$0")/.." && pwd)}"
LAMBDA="${LAMBDA:?set LAMBDA to the cross-toolchain installation (see README)}"
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"   # release layout: src/ include/ ld/ scripts/ board/
BOARD="${BOARD:-$ROOT/board/bbird}"  # the board port (see board/stub to add a CPU)
export PATH="$LAMBDA/xtool/bin:$PATH"
GHC=$LAMBDA/ghc-cross-install/bin/riscv32-oe-linux-ghc
GCC=riscv32-oe-linux-gcc
SRC="${1:?usage: build-hs.sh Prog.hs [out.elf] [program args]}"
OUT="${2:-${SRC%.hs}.elf}"
# a bare image has no shell to be given arguments at, so they are built in
ARGV="${3:-}"
[ -n "$ARGV" ] && ARGVDEF="-DBM_ARGV=\"$ARGV\"" || ARGVDEF=""
BASE="${SRC%.hs}"

LDIR=$("$GHC" --print-libdir)
L="$LDIR/riscv32-linux-ghc-9.6.7"
RTSINC="$L/rts-1.0.2/include"
HSRTS="$L/rts-1.0.2/libHSrts-1.0.2.a"
HSBASE="$L/base-4.18.3.0/libHSbase-4.18.3.0.a"
HSPRIM="$L/ghc-prim-0.10.0/libHSghc-prim-0.10.0.a"
HSBIG="$L/ghc-bignum-1.3/libHSghc-bignum-1.3.a"
CFFI="$L/rts-1.0.2/libCffi.a"
# a program that uses ByteString (or anything else outside base) needs its
# archive too: take whatever the cross tree has, the link drops what it does
# not reference.
HSEXTRA=$(echo "$L"/bytestring-*/libHSbytestring-*.a "$L"/deepseq-*/libHSdeepseq-*.a \
                "$L"/template-haskell-*/libHStemplate-haskell-*.a \
                "$L"/ghc-boot-th-*/libHSghc-boot-th-*.a \
                "$L"/pretty-*/libHSpretty-*.a "$L"/containers-*/libHScontainers-*.a \
                "$L"/array-*/libHSarray-*.a)
SYNC="$LDIR/librv32sync.a"

CF="-O2 -march=rv32imaf_zicsr_zifencei -mabi=ilp32 -static -no-pie -ffreestanding -DIS_SIM=1 -I$ROOT/include -I$BOARD -I$ROOT/board ${BM_EXTRA:-}"

# STDIN=<file>: embed the file as the image stdin (bm_stdin_data/len; the
# kernel serves it on fd 0).  Generated as C so the toolchain stays one.
STDINOBJ=""
if [ -n "${STDIN:-}" ]; then
  python3 -c "import sys;d=open(sys.argv[1],'rb').read();o=open(sys.argv[2],'w');o.write('const char bm_stdin_data[]={'+','.join(str(b) for b in d)+'};\nconst unsigned bm_stdin_len='+str(len(d))+';\n')" "$STDIN" "$BMOUT/stdin_blob.c"
  $GCC $CF -c "$BMOUT/stdin_blob.c" -o "$BMOUT/stdin_blob.o"
  STDINOBJ="$BMOUT/stdin_blob.o"
fi

# 1. Haskell -> object (unregisterised via-C)
COMPAT=$ROOT/ghc-compat
FMOBJ=""
if grep -q "Data.FiniteMap" "$SRC"; then
  "$GHC" -O2 -c "$COMPAT/Data/FiniteMap.hs" -o "$BMOUT/FiniteMap.o" -ohi "$COMPAT/Data/FiniteMap.hi"
  FMOBJ="$BMOUT/FiniteMap.o"
fi
"$GHC" -O2 -i"$COMPAT" -c "$SRC" -o "$BASE.o"

# 2. bmkernel objects + the C main
# the card: fat16 from thinOS over the HAL's sd driver, the pair that boots
# thinOS. Programs that never open a file still link them and never call them.
HAL="${HAL:-$(for h in "$HOME/work/tools/hal" "$HOME/work/hal"; do
                [ -d "$h" ] && { echo "$h"; break; }; done)}"
THINOS="${THINOS:-$(for t in "$HOME/work/tools/thinOS" "$HOME/work/thinOS"; do
                      [ -d "$t" ] && { echo "$t"; break; }; done)}"
$GCC $CF -I"$THINOS/include" -I"$HAL" -c "$THINOS/src/fat16.c" -o "$BMOUT/fat16.o"
$GCC $CF -c "$HAL/sd.c"                               -o "$BMOUT/sd.o"
$GCC $CF -c "$BOARD/board.c" -o "$BMOUT/board.o"
$GCC $CF -c "$ROOT/src/syscalls.c" -o "$BMOUT/syscalls.o"
$GCC $CF -c "$ROOT/src/bmperf.c"   -o "$BMOUT/bmperf.o"
$GCC $CF -c "$ROOT/src/phdrs.c"    -o "$BMOUT/phdrs.o"
$GCC $CF -c "$ROOT/src/atomics.c"  -o "$BMOUT/atomics.o"
$GCC $CF -c "$ROOT/src/start.S"    -o "$BMOUT/start.o"
$GCC $CF $ARGVDEF ${BMCFLAGS:-} -I"$ROOT/include" -I"$RTSINC" -c "$ROOT/src/bmmain.c" -o "$BMOUT/bmmain.o"
  # RTSSRC: the rts SOURCE tree of the exact GHC that built the linked
  # RTS (bmprof.c takes the Capability layout from it; no guessed offsets)
  RTSSRC="${RTSSRC:?set RTSSRC to the ghc source rts/ directory matching the cross GHC}"
  $GCC $CF -I"$ROOT/include" -I"$RTSINC" -I"$RTSSRC" -I"$RTSSRC/include" -c "$ROOT/src/bmprof.c" -o "$BMOUT/bmprof.o"

# 3. bare link. Keep glibc crt1.o (its _start); ENTRY=_reset (bmkernel).
#    glibc/libm/libgcc satisfy the RTS's libc/OS calls (mmap/clock/pthread/...);
#    those become ecalls the bmkernel services. libCffi = RTS FFI; librv32sync
#    = rv32 64-bit __sync_*. --allow-multiple-definition lets our exit/abort win.
$GCC $CF -T "$ROOT/ld/bm.ld" -Wl,-e,_reset -Wl,-Map="${LINKMAP:-/dev/null}" \
  -Wl,--no-warn-rwx-segments -Wl,--allow-multiple-definition \
  "$BMOUT/start.o" "$BMOUT/board.o" "$BMOUT/syscalls.o" "$BMOUT/bmperf.o" "$BMOUT/phdrs.o" \
  "$BMOUT/fat16.o" "$BMOUT/sd.o" \
  "$BMOUT/bmmain.o" "$BMOUT/atomics.o" "$BMOUT/bmprof.o" "$BASE.o" $STDINOBJ $FMOBJ \
  -Wl,--start-group \
    $HSEXTRA "$HSBASE" "$HSPRIM" "$HSBIG" "$HSRTS" "$CFFI" \
    -lc -lm -lgcc \
  -Wl,--end-group \
  -o "$OUT"

RE=$(ls $LAMBDA/work/build/tmp-glibc/work/*/ghc-cross/*/recipe-sysroot-native/usr/bin/riscv32-oe-linux/riscv32-oe-linux-readelf | head -1)
"$RE" -h "$OUT" | grep -iE "Entry|Machine|Flags"
"${RE%readelf}size" "$OUT" 2>/dev/null || true
echo "built $OUT"
