/* scheduler.c - Cooperative scheduler (no timer preemption) */

#include "process.h"
#include "pit.h"
#include "kprintf.h"

/* Scheduler tick handler - called by PIT but does NOT preempt */
void scheduler_tick(void) {
    /* Preemption DISABLED - timer only increments ticks for stats */
    process_t *current = process_current();
    if (current) {
        current->ticks++;
    }
}

/* Initialize scheduler - cooperative mode (no automatic preemption) */
void scheduler_init(void) {
    /* Install timer callback for statistics ONLY - no context switching */
    pit_install_callback(scheduler_tick);
    kprintf("Scheduler initialized (cooperative mode - no preemption)\n");
    kprintf("  - Timer running at 100Hz for statistics only\n");
    kprintf("  - Context switches happen only via explicit yield()\n");
}