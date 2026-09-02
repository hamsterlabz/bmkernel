# fun_config.S - configure the fn.* machine and run the graph reduction, called
# as a normal C function by fun_main.c. Seeds the fun machine (heap ptr csrw
# 0x7c0, graph base csrw 0x7c1) and enters the graph (fn.elink), exactly like
# sw/xfun/hs/startup_check.S; the WHNF continuation's fn.tor pulls the answer
# into a0 and we return it.
#
# ra/sp are stashed in .bss across the reduction: the reducer walks the vspine +
# heap and may clobber the C regs, so we cannot assume they survive. Assemble
# with the xfun gas (fn.* opcodes).

  .text
  .balign 4
  .globl fun_config
fun_config:
  la    t0, _fc_save
  sw    ra, 0(t0)
  sw    sp, 4(t0)

  la    t1, _fungraph_heap        # node heap (grows up); seed hp
  csrw  0x7c0, t1
  li   t1, 0x87FE0000
  csrw 0x7e4, t1          # RSTACK_START
  li   t1, 0x88000000
  csrw 0x7e5, t1          # RSTACK_END
  la    t1, _whnf_epi             # WHNF epilogue: pops the software frame stack
  csrw  0x7c1, t1

  la    t1, _fc_entry
  jalr  x0, 0(t1)                 # ID-resolved: csrw retired before fn.* fetch
  nop
  nop
  nop
  nop

  .balign 32
_fc_entry:
  .word _fc_epi                   # link cell: the WHNF continuation
  fn.elink bench_graph            # enter the graph (gmhs `main`, renamed)

  .balign 4
_fc_epi:
  .word 0x0000255b                # fn.tor a0 <- the answer box payload
  la    t0, _fc_save
  lw    ra, 0(t0)
  lw    sp, 4(t0)
  ret                             # return the answer (a0) to fun_main

  .balign 4
  .globl _error0
_error0:
  la    t0, fun_error
  jr    t0

  .section .bss
  .balign 4
_fc_save: .space 8                # [ra, sp]

# The bundled collector's ROOT SET.  startup_perf.S carries these for the
# fun.ld path; this harness links no startup_perf.o, so it must carry them
# itself or the listing's collector has nothing to scan.  Same shapes and
# sizes as startup_perf.S.
  .data
  .balign 4
  .globl fn_rootsp, fn_cafn, fn_gcverbose, fn_roots, fn_caf
fn_rootsp:    .word 0
fn_cafn:      .word 0
fn_gcverbose: .word 0
  .balign 4
fn_roots:     .skip 256
fn_caf:       .skip 256
