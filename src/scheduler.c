/*
 * scheduler.c - Ready-queue orderings for each scheduling algorithm.
 *
 * Tie-breaking follows Section 7 of the specification exactly:
 *   FCFS     : arrival time, then lexicographically smaller ID
 *   PRIORITY : priority value, then arrival time, then ID
 *   SJF      : service time, then arrival time, then ID
 *   RR       : position in the FIFO ready queue
 */
#include "scheduler.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

static int cmp_int(int a, int b)
{
    return (a > b) - (a < b);
}

int compare_arrival(const Process *a, const Process *b)
{
    int c = cmp_int(a->arrival, b->arrival);
    return c ? c : strcmp(a->id, b->id);
}

int compare_priority(const Process *a, const Process *b)
{
    int c = cmp_int(a->priority, b->priority);
    return c ? c : compare_arrival(a, b);
}

static int compare_sjf(const Process *a, const Process *b)
{
    int c = cmp_int(a->service, b->service);
    return c ? c : compare_arrival(a, b);
}

/* Round Robin: whoever entered the ready queue first. ready_seq is unique
 * and the simulator assigns it in the order required by Section 7.4. */
static int compare_fifo(const Process *a, const Process *b)
{
    return (a->ready_seq > b->ready_seq) - (a->ready_seq < b->ready_seq);
}

static const Policy POLICIES[ALG_COUNT] = {
    { ALG_FCFS, "FCFS",
      "First-Come First-Served (non-preemptive)",
      1, 0, 0, compare_arrival },
    { ALG_PRIORITY, "PRIORITY",
      "Priority scheduling (non-preemptive)",
      1, 0, 0, compare_priority },
    { ALG_PRIORITY_PREEMPTIVE, "PRIORITY_PREEMPTIVE",
      "Priority scheduling (preemptive) [optional extra]",
      0, 1, 0, compare_priority },
    { ALG_RR, "RR",
      "Round Robin [optional extra]",
      0, 0, 1, compare_fifo },
    { ALG_SJF, "SJF",
      "Shortest Job First (non-preemptive) [optional extra]",
      0, 0, 0, compare_sjf },
};

const Policy *policy_lookup(const char *name)
{
    for (int i = 0; i < ALG_COUNT; i++)
        if (strcasecmp(name, POLICIES[i].name) == 0)
            return &POLICIES[i];
    return NULL;
}

const Policy *policy_get(Algorithm alg)
{
    return (alg >= 0 && alg < ALG_COUNT) ? &POLICIES[alg] : NULL;
}
