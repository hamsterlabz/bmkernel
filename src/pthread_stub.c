/* pthread_stub.c - single-thread pthread shim for the bare bmkernel.
 * The shootout entries use pthreads only to divide work and join;  on one
 * hart, pthread_create runs the routine INLINE and join returns its value.
 * Linked BEFORE -lc with --allow-multiple-definition, so these override
 * glibc's NPTL (whose clone() the kernel does not provide).  Locks are
 * no-ops: with inline execution there is no concurrency to guard. */
typedef unsigned long pt_t;
static void *pt_ret[64];
static int pt_n = 0;

int pthread_create(pt_t *t, const void *attr, void *(*fn)(void *), void *arg)
{
    (void)attr;
    pt_ret[pt_n & 63] = fn(arg);
    *t = (pt_t)pt_n++;
    return 0;
}
int pthread_join(pt_t t, void **ret)
{
    if (ret) *ret = pt_ret[t & 63];
    return 0;
}
int pthread_mutex_init(void *m, const void *a)   { (void)m; (void)a; return 0; }
int pthread_mutex_lock(void *m)                  { (void)m; return 0; }
int pthread_mutex_trylock(void *m)               { (void)m; return 0; }
int pthread_mutex_unlock(void *m)                { (void)m; return 0; }
int pthread_mutex_destroy(void *m)               { (void)m; return 0; }
int pthread_cond_init(void *c, const void *a)    { (void)c; (void)a; return 0; }
int pthread_cond_wait(void *c, void *m)          { (void)c; (void)m; return 0; }
int pthread_cond_signal(void *c)                 { (void)c; return 0; }
int pthread_cond_broadcast(void *c)              { (void)c; return 0; }
int pthread_cond_destroy(void *c)                { (void)c; return 0; }
