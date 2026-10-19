/*
 * metrics.h - Scheduling metrics (Section 10 of the specification).
 *
 *   TAT_i = C_i - A_i            (turnaround)
 *   RT_i  = S_i - A_i            (response, uses FIRST start time)
 *   WT_i  = TAT_i - B_i          (waiting, includes every READY period)
 *   Throughput  = N / (last completion - first arrival)
 *   Utilization = B / (M * E) * 100, with E = last completion - first arrival
 */
#ifndef METRICS_H
#define METRICS_H

#include "process.h"
#include "simulator.h"

typedef struct {
    int    completed;        /* N */
    int    first_arrival;
    int    last_completion;
    int    elapsed;          /* E = last_completion - first_arrival */
    long   busy_time;        /* B, summed over both librarians */
    double avg_waiting;
    double avg_turnaround;
    double avg_response;
    double avg_completion;
    int    max_waiting;      /* worst single wait: a starvation indicator */
    int    max_waiting_proc; /* index of the reader that waited longest */
    double throughput;       /* readers per time unit */
    double utilization;      /* percent */
} Summary;

int  turnaround_time(const Process *p);
int  response_time(const Process *p);
int  waiting_time(const Process *p);

void compute_summary(const Workload *w, const SimResult *r, Summary *s);

#endif /* METRICS_H */
