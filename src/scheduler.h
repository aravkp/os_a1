/*
 * scheduler.h - Scheduling policies.
 *
 * A policy is just an ordering over READY processes ("who should get the next
 * free librarian?") plus two flags describing how the simulator should treat
 * running processes (priority preemption, time-slicing). Keeping the policy
 * separate from the simulation engine means every algorithm shares the same
 * event loop, so differences in the results come only from the policy.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

typedef enum {
    ALG_FCFS,
    ALG_PRIORITY,
    ALG_PRIORITY_PREEMPTIVE,
    ALG_RR,
    ALG_SJF,
    ALG_COUNT
} Algorithm;

/* Returns < 0 if `a` should be scheduled before `b`, > 0 if after.
 * Every comparator ends with a process-ID tie-break so it never returns 0
 * for two distinct processes; this is what makes the simulation deterministic. */
typedef int (*ProcCompare)(const Process *a, const Process *b);

typedef struct {
    Algorithm   alg;
    const char *name;          /* CLI name, e.g. "PRIORITY" */
    const char *description;
    int         required;      /* 1 = required for Smart Library, 0 = optional extra */
    int         preemptive;    /* 1 = strictly higher priority arrival preempts */
    int         time_sliced;   /* 1 = Round Robin quantum applies */
    ProcCompare compare;       /* ordering of the ready queue */
} Policy;

/* Look up a policy by its CLI name (case-insensitive). NULL if unknown. */
const Policy *policy_lookup(const char *name);

/* Policy by enum value. */
const Policy *policy_get(Algorithm alg);

/* Priority order used to pick a preemption victim (Section 7.3):
 * the "largest" running process under this order is preempted first. */
int compare_priority(const Process *a, const Process *b);

/* Arrival order with ID tie-break (FCFS order). */
int compare_arrival(const Process *a, const Process *b);

#endif /* SCHEDULER_H */
