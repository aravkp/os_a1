/*
 * metrics.c - Per-process and whole-run scheduling metrics.
 */
#include "metrics.h"

#include <string.h>

int turnaround_time(const Process *p) { return p->completion - p->arrival; }
int response_time(const Process *p)   { return p->start - p->arrival; }
int waiting_time(const Process *p)    { return turnaround_time(p) - p->service; }

void compute_summary(const Workload *w, const SimResult *r, Summary *s)
{
    long sum_wt = 0, sum_tat = 0, sum_rt = 0, sum_c = 0;

    memset(s, 0, sizeof *s);
    s->max_waiting = -1;
    s->max_waiting_proc = -1;

    for (int i = 0; i < w->n; i++) {
        const Process *p = &w->procs[i];
        if (p->state != STATE_TERMINATED)
            continue;

        if (s->completed == 0 || p->arrival < s->first_arrival)
            s->first_arrival = p->arrival;
        if (s->completed == 0 || p->completion > s->last_completion)
            s->last_completion = p->completion;
        s->completed++;

        int wt = waiting_time(p);
        sum_wt  += wt;
        sum_tat += turnaround_time(p);
        sum_rt  += response_time(p);
        sum_c   += p->completion;
        if (wt > s->max_waiting) {
            s->max_waiting = wt;
            s->max_waiting_proc = i;
        }
    }
    if (s->completed == 0)
        return;

    s->elapsed   = s->last_completion - s->first_arrival;
    s->busy_time = r->busy_time;

    s->avg_waiting    = (double)sum_wt  / s->completed;
    s->avg_turnaround = (double)sum_tat / s->completed;
    s->avg_response   = (double)sum_rt  / s->completed;
    s->avg_completion = (double)sum_c   / s->completed;

    /* Service time is always >= 1, so elapsed > 0 whenever N >= 1. */
    s->throughput  = (double)s->completed / s->elapsed;
    s->utilization = 100.0 * (double)s->busy_time / ((double)NUM_SERVICE_UNITS * s->elapsed);
}
