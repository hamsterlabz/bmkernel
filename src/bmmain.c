void bb_uart_setbaud(unsigned);   /* shared HAL */
/* bmmain.c - the C main() for a bare-metal GHC program. crt0/_reset jumps to
 * glibc _start -> __libc_start_main -> main (here). We start the RTS with the
 * flags that switch off the OS facilities the nanokernel does not provide:
 *   -V0                          no interval timer (no timerfd/SIGVTALRM)
 *   --install-signal-handlers=no no sigaction
 * The RTS is the non-threaded (way v) build, so no pthreads/epoll event mgr.
 * hs_main runs Main.main then shuts the RTS down and calls exit() (our override
 * -> PMC report + tohost). */
#include "Rts.h"

extern StgClosure ZCMain_main_closure;

int main(int argc, char *argv[])
{
#ifdef IS_SIM
  bb_uart_setbaud(15);   /* 16 clk/bit, what the bench decodes */
#else
  bb_uart_setbaud(867);  /* 115200 at 100 MHz */
#endif

    (void)argc; (void)argv;                 /* crt0 gives argc=1, argv[0]="ghc-bare" */
    static char a0[] = "ghc-bare";
    static char r0[] = "+RTS";
    static char r1[] = "-V0";
    static char r2[] = "--install-signal-handlers=no";
    static char r3[] = "-RTS";
    /* Program arguments, for an image built with -DBM_ARGV="a b c": there is
     * no shell here to type them at. Split on spaces in place; the RTS flags
     * keep their place after them. */
#ifdef BM_ARGV
    static char args[] = BM_ARGV;
    char *av[16]; int n = 0;
    av[n++] = a0;
    for (char *p = args; *p && n < 11; ) {
      while (*p == ' ') p++;
      if (!*p) break;
      av[n++] = p;
      while (*p && *p != ' ') p++;
      if (*p) *p++ = 0;
    }
    av[n++] = r0; av[n++] = r1; av[n++] = r2; av[n++] = r3; av[n] = 0;
#else
    char *av[] = { a0, r0, r1, r2, r3, 0 };
    const int n = 5;
#endif
    RtsConfig conf = defaultRtsConfig;
    conf.rts_opts_enabled = RtsOptsAll;
#ifdef BM_HEAP_CENSUS
#define BM_RTS_OPTS "-T -hT -A32m -G1" /* censuses come ONLY from the testbench timer (ghcprof.inc pokes performHeapProfile + rHpLim); big nursery = no organic GCs, -G1 so each forced GC is major and census-bearing */
#endif
#ifndef BM_RTS_OPTS
#define BM_RTS_OPTS "-T -A32m"   /* big allocation area: perf runs never collect */
#endif
    conf.rts_opts = BM_RTS_OPTS; /* the heap-profile variant overrides with
                                    -hT and a small area so censuses happen */   /* GC stats for getRTSStats: the bare-metal
                               -with-rtsopts (ghc_rts_opts is only read by
                               GHC's own generated main, not by hs_main) */
    /* VERBATIM images (build-hs.sh): bracket exactly Main.main with the
     * bmperf region via the RTS embedding API, so the main-loop PMC block
     * excludes RTS init and shutdown -- hs_main would run all three phases
     * behind one call and leave nothing to window. */
    {
        int argc = n; char **argv = av;
        hs_init_ghc(&argc, &argv, conf);
        {
            extern void *bmperf_alloc(void);
            extern void bmperf_snap(void *);
            extern void bmperf_report(void *, void *);
            void *s_ = bmperf_alloc(), *e_ = bmperf_alloc();
            Capability *cap = rts_lock();
            HaskellObj ret;
            bmperf_snap(s_);
            rts_evalLazyIO(&cap, rts_apply(cap,
                (HaskellObj)runIO_closure, (HaskellObj)&ZCMain_main_closure), &ret);
            bmperf_snap(e_);
            bmperf_report(s_, e_);
            rts_checkSchedStatus("main", cap);
            rts_unlock(cap);
        }
        hs_exit();
        return 0;
    }
}
