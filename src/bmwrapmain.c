/* bmwrapmain.c - link-level main window for VERBATIM C benchmarks:
 * built with -Wl,--wrap=main, so the unmodified program's main becomes
 * __real_main and this wrapper brackets it with the bmperf region
 * (mcountinhibit window + main-loop PMC block), exactly what the
 * bench-wrapped suites do from source. */
extern void *bmperf_alloc(void);
extern void bmperf_snap(void *);
extern void bmperf_report(void *, void *);
extern int __real_main(int argc, char **argv, char **envp);

int __wrap_main(int argc, char **argv, char **envp)
{
    void *s = bmperf_alloc(), *e = bmperf_alloc();
    bmperf_snap(s);
    int r = __real_main(argc, argv, envp);
    bmperf_snap(e);
    bmperf_report(s, e);
    return r;
}
