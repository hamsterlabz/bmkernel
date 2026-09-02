# start.S - bare-metal reset + ecall trap dispatcher for running a static
# glibc rv32ima/ilp32 binary (incl. the GHC RTS) with no Linux kernel.
#
# _reset (ELF entry, first in .text.init at RAM base): set gp/sp, enable F,
# point mtvec at trap_vec, build the Linux initial process stack
# (argc/argv/envp/auxv) at the top of RAM, then jump to glibc's _start.
# glibc's syscalls (ecall) trap to trap_vec -> bm_syscall() in syscalls.c.
	.section .text.init,"ax"
	.global _reset
_reset:
	.option push
	.option norelax
	la    gp, __global_pointer$
	.option pop
	la    sp, _estack

	# enable the SoC's F extension: mstatus.FS = Dirty (bits 14:13 = 11),
	# clear fcsr. The ABI is ilp32 (soft-float calling convention), but the
	# hardware FPU is present and enabled so F instructions are usable.
	li    t0, 0x6000
	csrs  mstatus, t0
	fscsr zero

	# trap vector (direct mode; trap_vec is 4-aligned)
	la    t0, trap_vec
	csrw  mtvec, t0

	# Board bring-up (UART divisors, clocks): the board port owns every
	# SoC-specific constant.  sp/gp are live; nothing has printed yet.
	call  board_early_init

	# zero .bss (no loader does it bare-metal; glibc _start assumes it is done)
	la    a0, _sbss
	la    a1, _ebss
0:	bgeu  a0, a1, 1f
	sw    zero, 0(a0)
	addi  a0, a0, 4
	j     0b
1:

	# Snapshot the PMCs at reset (post-.bss, pre-glibc) so the whole-program
	# "full-run" window covers the ENTIRE runtime: glibc/RTS startup, the
	# one-time FDE-table sort, and main. bm_perf_start is idempotent (its
	# .init_array constructor then no-ops), so THIS earliest call is the window
	# start. _reset never returns, so clobbering ra/caller-saved regs is fine.
	call  bm_perf_start

	# Build the initial process stack that glibc's _start expects:
	#   sp -> [argc][argv0][NULL][envp NULL][auxv...][AT_NULL,0]
	# Lay it just below _estack, 16-byte aligned, and hand glibc the rest
	# of the 8 MB region below as its stack.
	la    sp, _estack
	li    t0, 256
	sub   sp, sp, t0            # reserve the block (argv may hold 16 args), 16-align
	andi  sp, sp, -16

	# argc/argv: a builder-generated argv_blob.c may define bm_argc (int)
	# and bm_argv_vec (char*[], NULL-terminated).  Weak: absent, the
	# classic single-arg block is built.  With N args the block layout is
	# [argc][argv0..argvN-1][NULL][envp NULL] and the auxv follows at a
	# variable offset, so the auxv writes below index off t1 (block cursor).
	.weak bm_argc
	.weak bm_argv_vec
	la    t1, bm_argc
	beqz  t1, 3f
	lw    t0, 0(t1)
	sw    t0, 0(sp)             # argc = bm_argc
	la    t2, bm_argv_vec       # copy pointers up to and incl. the NULL
	addi  t1, sp, 4
2:	lw    t3, 0(t2)
	sw    t3, 0(t1)
	addi  t2, t2, 4
	addi  t1, t1, 4
	bnez  t3, 2b
	sw    zero, 0(t1)           # envp[0] = NULL
	addi  t1, t1, 4
	j     4f
3:	li    t0, 1
	sw    t0, 0(sp)             # argc = 1
	la    t0, bm_arg0
	sw    t0, 4(sp)             # argv[0] = "ghc-bare"
	sw    zero, 8(sp)           # argv[1] = NULL
	sw    zero, 12(sp)          # envp[0] = NULL
	addi  t1, sp, 16            # auxv cursor for the classic block
4:
	# auxv, cursor-relative (t1 = first slot after envp NULL): the argv
	# block above is variable-length now.
	li    t0, 6                 # AT_PAGESZ
	sw    t0, 0(t1)
	li    t0, 4096
	sw    t0, 4(t1)
	li    t0, 25                # AT_RANDOM
	sw    t0, 8(t1)
	la    t0, bm_atrandom
	sw    t0, 12(t1)
	li    t0, 3                 # AT_PHDR -> synthesized phdr table (phdrs.c)
	sw    t0, 16(t1)
	la    t0, bm_phdrs
	sw    t0, 20(t1)
	li    t0, 4                 # AT_PHENT = sizeof(Elf32_Phdr)
	sw    t0, 24(t1)
	li    t0, 32
	sw    t0, 28(t1)
	li    t0, 5                 # AT_PHNUM
	sw    t0, 32(t1)
	lw    t0, bm_phnum          # value
	sw    t0, 36(t1)
	sw    zero, 40(t1)          # AT_NULL
	sw    zero, 44(t1)

	# glibc _start reads a0..a2 = dl_fini? no: it reads argc/argv from sp.
	la    t0, _start
	jr    t0

	.section .rodata
bm_arg0:      .asciz "ghc-bare"
	.section .data
	.align 4
bm_atrandom:  .word 0x9e3779b9, 0x243f6a88, 0xb7e15162, 0x8aed2a6a

# --- ecall trap dispatcher --------------------------------------------------
# On ecall from M-mode (mcause=11) service a Linux syscall: a7=nr, a0..a5=args,
# result -> a0, mepc += 4. Any other trap -> bm_trap_fatal() (prints + halts).
	.section .text
	.align 4
	.global trap_vec
trap_vec:
	addi  sp, sp, -16*4
	sw    ra, 0(sp)
	sw    t0, 1*4(sp);  sw t1, 2*4(sp);  sw t2, 3*4(sp)
	sw    a0, 4*4(sp);  sw a1, 5*4(sp);  sw a2, 6*4(sp);  sw a3, 7*4(sp)
	sw    a4, 8*4(sp);  sw a5, 9*4(sp);  sw a6,10*4(sp);  sw a7,11*4(sp)
	sw    t3,12*4(sp);  sw t4,13*4(sp);  sw t5,14*4(sp);  sw t6,15*4(sp)

	csrr  t0, mcause
	li    t1, 11                # ecall from M-mode
	beq   t0, t1, 1f
	li    t1, 8                 # ecall from U-mode (if ever dropped to U)
	beq   t0, t1, 1f
	# non-ecall trap: fatal. Pass a pointer to the saved GPR frame (a3=sp) so
	# the handler can dump ra/a0../t.. - epc=0 tells us we jumped through NULL,
	# ra tells us who did it.
	csrr  a0, mcause
	csrr  a1, mepc
	csrr  a2, mtval
	mv    a3, sp
	call  bm_trap_fatal
	# fallthrough (never returns)

1:	# a7 = syscall nr, a0..a5 = args (still in the caller regs we saved)
	lw    a0, 4*4(sp)           # arg0
	lw    a1, 5*4(sp)
	lw    a2, 6*4(sp)
	lw    a3, 7*4(sp)
	lw    a4, 8*4(sp)
	lw    a5, 9*4(sp)
	lw    a6, 11*4(sp)          # a6 = syscall number (a7 in reg, saved at slot 11)
	call  bm_syscall           # returns result in a0
	sw    a0, 4*4(sp)           # write result back into a0 slot

	# advance past the ecall
	csrr  t0, mepc
	addi  t0, t0, 4
	csrw  mepc, t0

	lw    ra, 0(sp)
	lw    t0, 1*4(sp);  lw t1, 2*4(sp);  lw t2, 3*4(sp)
	lw    a0, 4*4(sp);  lw a1, 5*4(sp);  lw a2, 6*4(sp);  lw a3, 7*4(sp)
	lw    a4, 8*4(sp);  lw a5, 9*4(sp);  lw a6,10*4(sp);  lw a7,11*4(sp)
	lw    t3,12*4(sp);  lw t4,13*4(sp);  lw t5,14*4(sp);  lw t6,15*4(sp)
	addi  sp, sp, 16*4
	mret
