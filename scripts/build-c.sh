#!/usr/bin/env bash
# build-c.sh <prog.c> [out.elf] - link a static-glibc C program to run bare-metal
# on a bare RV32 CPU via the bmkernel (ecall syscall emulation). Validates the
# OS layer before layering GHC on top.
# LAMBDA = the cross-toolchain installation (see README, Toolchain).
# BMOUT = where objects and the ELF land.
READELF="${READELF:-$(command -v riscv32-oe-linux-readelf || echo /opt/riscv/bin/riscv32-unknown-elf-readelf)}"
SIZE="${SIZE:-$(command -v riscv32-oe-linux-size || echo /opt/riscv/bin/riscv32-unknown-elf-size)}"
BMOUT="${BMOUT:-./bmout}"; mkdir -p "$BMOUT"
BMARKS="${BMARKS:-$(cd "$(dirname "$0")/.." && pwd)}"
LAMBDA="${LAMBDA:?set LAMBDA to the cross-toolchain installation (see README)}"
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"   # release layout: src/ include/ ld/ scripts/ board/
BOARD="${BOARD:-$ROOT/board/bbird}"  # the board port (see board/stub to add a CPU)
export PATH="$LAMBDA/xtool/bin:$PATH"   # riscv32-oe-linux-gcc wrapper (sysroot+march+ilp32)
GCC=riscv32-oe-linux-gcc
SRC="${1:?usage: build-c.sh prog.c [out.elf]}"
OUT="${2:-$BMOUT/$(basename "${SRC%.c}").elf}"
# BSP built with F available (SoC has the FPU); ABI stays ilp32 (soft-float
# calling convention) so it links with the rv32ima/ilp32 GHC objects.
# -DIS_SIM=1 selects the start.S UART divisor 15 (16 clk/bit), which is what
# the testbench receiver decodes. Without it start.S programs 867 clk/bit
# (115200 @ 100 MHz, the FPGA value): output is unreadable AND every
# character costs ~8670 cycles, so a do-nothing main() measured 7,555,067
# cycles. build-hs/gadt/nofib/flite all define it; this script did not.
OPT="${OPT:--O2}"
CF="$OPT -march=rv32imaf_zicsr_zifencei -mabi=ilp32 -static -no-pie -ffreestanding -DIS_SIM=1 -I$ROOT/include -I$BOARD -I$ROOT/board"

# STDIN=<file>: embed the file as the image stdin (bm_stdin_data/len; the
# kernel serves it on fd 0).  Generated as C so the toolchain stays one.
# ARGV="a1 a2 ...": program arguments; generates argv_blob.c with bm_argc /
# bm_argv_vec, which start.S (weak symbols) folds into the initial stack.
ARGVOBJ=""
if [ -n "${ARGV:-}" ]; then
  python3 -c "
import sys
args = sys.argv[1].split()
o = open(sys.argv[2], 'w')
o.write('const char bm_c_arg0[] = \"c-bare\";\n')
for i, a in enumerate(args):
    o.write('const char bm_c_arg%d[] = \"%s\";\n' % (i+1, a))
vec = ', '.join('(char*)bm_c_arg%d' % i for i in range(len(args)+1))
o.write('char * const bm_argv_vec[] = { %s, 0 };\n' % vec)
o.write('const int bm_argc = %d;\n' % (len(args)+1))
" "$ARGV" "$BMOUT/argv_blob.c"
  $GCC $CF -c "$BMOUT/argv_blob.c" -o "$BMOUT/argv_blob.o"
  ARGVOBJ="$BMOUT/argv_blob.o"
fi

STDINOBJ=""
if [ -n "${STDIN:-}" ]; then
  python3 -c "import sys;d=open(sys.argv[1],'rb').read();o=open(sys.argv[2],'w');o.write('const char bm_stdin_data[]={'+','.join(str(b) for b in d)+'};\nconst unsigned bm_stdin_len='+str(len(d))+';\n')" "$STDIN" "$BMOUT/stdin_blob.c"
  $GCC $CF -c "$BMOUT/stdin_blob.c" -o "$BMOUT/stdin_blob.o"
  STDINOBJ="$BMOUT/stdin_blob.o"
fi

$GCC $CF -c "$BOARD/board.c" -o "$BMOUT/board.o"
$GCC $CF -c "$ROOT/src/syscalls.c" -o "$BMOUT/syscalls.o"
$GCC $CF -c "$ROOT/src/bmperf.c"   -o "$BMOUT/bmperf.o"
$GCC $CF -c "$ROOT/src/start.S"    -o "$BMOUT/start.o"
$GCC $CF -c "$ROOT/src/phdrs.c"    -o "$BMOUT/phdrs.o"
$GCC $CF -c "$SRC"             -o "$BMOUT/$(basename "${SRC%.c}").o"
$GCC $CF -c "$ROOT/src/pthread_stub.c" -o "$BMOUT/pthread_stub.o"
$GCC $CF -c "$ROOT/src/bmwrapmain.c" -o "$BMOUT/bmwrapmain.o"

# keep glibc crt1.o (its _start); override ENTRY to _reset (bmkernel reset).
$GCC $CF -T "$ROOT/ld/bm.ld" -Wl,-e,_reset -Wl,--wrap=main -Wl,--no-warn-rwx-segments -Wl,--allow-multiple-definition \
  "$BMOUT/start.o" "$BMOUT/board.o" "$BMOUT/syscalls.o" "$BMOUT/bmperf.o" "$BMOUT/phdrs.o" "$BMOUT/$(basename "${SRC%.c}").o" "$BMOUT/pthread_stub.o" "$BMOUT/bmwrapmain.o" \
  -Wl,--start-group -lc -lm -lgcc -Wl,--end-group \
  $STDINOBJ $ARGVOBJ -o "$OUT"

"$READELF" -h "$OUT" | grep -iE "Entry|Machine|Flags"
"$SIZE" "$OUT"
echo "built $OUT"
