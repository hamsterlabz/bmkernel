/* bmprof.c -- exports the two RTS addresses the testbench profile "timer"
 * pokes for fig4-style heap sampling: performHeapProfile (census request
 * flag) and MainCapability.r.rHpLim (poked to 0 to interrupt the mutator
 * at the next heap check, exactly what the RTS itimer tick does).  The
 * Capability layout comes from the SAME rts source tree that built the
 * linked RTS -- no guessed offsets. */
#include "Rts.h"
#include "Capability.h"
extern bool performHeapProfile;
void *bm_php_addr   = &performHeapProfile;
void *bm_hplim_addr = &MainCapability.r.rHpLim;

/* called (weakly) by bmperf_report at window freeze: no census may run
 * after the compute window -- clear the pending request AND the master
 * enable, so hs_exit's unconditional final census is off too. */
void bm_census_off(void){
    performHeapProfile = 0;
    RtsFlags.ProfFlags.doHeapProfile = 0;
}
