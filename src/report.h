/*
 * report.h - Output formatting. The simulation never depends on any of this.
 */
#ifndef REPORT_H
#define REPORT_H

#include <stdio.h>

#include "metrics.h"
#include "process.h"
#include "scheduler.h"
#include "simulator.h"

/* Standardized machine-readable output (Section 15). */
void report_machine(FILE *out, const Workload *w, const Summary *s);

/* Human-readable extras used with --verbose. */
void report_header(FILE *out, const Workload *w, const Policy *p);
void report_trace(FILE *out, const Workload *w, const SimResult *r);
void report_gantt(FILE *out, const Workload *w, const SimResult *r, const Summary *s);
void report_table(FILE *out, const Workload *w, const SimResult *r, const Summary *s);

/* Side-by-side comparison of several algorithms on the same input. */
void report_compare_header(FILE *out, const Workload *w);
void report_compare_row(FILE *out, const Workload *w, const Policy *p,
                        const SimResult *r, const Summary *s);
void report_compare_footer(FILE *out);

#endif /* REPORT_H */
