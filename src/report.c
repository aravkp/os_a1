/*
 * report.c - Machine-readable output plus human-readable trace, Gantt chart,
 * result table and algorithm comparison.
 */
#include "report.h"

#include <stdlib.h>
#include <string.h>

#define GANTT_WIDTH 64

/* ------------------------------------------------------------------ */
/* Machine-readable output (Section 15)                                */
/* ------------------------------------------------------------------ */

static const Workload *sort_ctx;

static int cmp_index_by_id(const void *a, const void *b)
{
    return strcmp(sort_ctx->procs[*(const int *)a].id,
                  sort_ctx->procs[*(const int *)b].id);
}

/* Indices of all processes in lexicographic ID order. Caller frees. */
static int *ids_sorted(const Workload *w)
{
    int *idx = malloc((size_t)w->n * sizeof *idx);
    if (!idx)
        return NULL;
    for (int i = 0; i < w->n; i++)
        idx[i] = i;
    sort_ctx = w;
    qsort(idx, (size_t)w->n, sizeof *idx, cmp_index_by_id);
    return idx;
}

void report_machine(FILE *out, const Workload *w, const Summary *s)
{
    int *idx = ids_sorted(w);
    if (!idx)
        return;

    fprintf(out, "PROCESS_ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,TURNAROUND,RESPONSE\n");
    for (int k = 0; k < w->n; k++) {
        const Process *p = &w->procs[idx[k]];
        if (p->state != STATE_TERMINATED)
            continue;
        fprintf(out, "%s,%d,%d,%d,%d,%d,%d,%d,%d\n",
                p->id, p->arrival, p->priority, p->service, p->start, p->completion,
                waiting_time(p), turnaround_time(p), response_time(p));
    }
    fprintf(out, "SUMMARY\n");
    fprintf(out, "AVERAGE_WAITING,%.2f\n", s->avg_waiting);
    fprintf(out, "AVERAGE_TURNAROUND,%.2f\n", s->avg_turnaround);
    fprintf(out, "AVERAGE_RESPONSE,%.2f\n", s->avg_response);
    fprintf(out, "THROUGHPUT,%.4f\n", s->throughput);
    fprintf(out, "UTILIZATION,%.2f\n", s->utilization);
    free(idx);
}

/* ------------------------------------------------------------------ */
/* Human-readable output                                               */
/* ------------------------------------------------------------------ */

void report_header(FILE *out, const Workload *w, const Policy *p)
{
    fprintf(out, "\n=== Smart Library Help Desk - %s ===\n", p->description);
    fprintf(out, "Readers: %d   Librarians: %d   RR quantum: %d\n",
            w->n, NUM_SERVICE_UNITS, w->quantum);
}

void report_trace(FILE *out, const Workload *w, const SimResult *r)
{
    fprintf(out, "\n--- Event trace ---\n");
    for (int i = 0; i < r->nev; i++) {
        const Event *e = &r->events[i];
        const Process *p = e->proc >= 0 ? &w->procs[e->proc] : NULL;
        char info[96];
        const char *transition = "";

        switch (e->type) {
        case EV_ARRIVAL:
            snprintf(info, sizeof info, "%s (%s, pri %d, needs %d)",
                     p->id, p->category, p->priority, p->service);
            transition = "NEW -> READY";
            break;
        case EV_START:
        case EV_RESUME:
            snprintf(info, sizeof info, "%s on Librarian %d", p->id, e->unit + 1);
            transition = "READY -> RUNNING";
            break;
        case EV_PREEMPT:
            snprintf(info, sizeof info, "%s off Librarian %d for %s",
                     p->id, e->unit + 1, w->procs[e->other].id);
            transition = "RUNNING -> READY";
            break;
        case EV_QUANTUM:
            snprintf(info, sizeof info, "%s off Librarian %d (quantum used)",
                     p->id, e->unit + 1);
            transition = "RUNNING -> READY";
            break;
        case EV_COMPLETE:
            snprintf(info, sizeof info, "%s leaves Librarian %d", p->id, e->unit + 1);
            transition = "RUNNING -> TERMINATED";
            break;
        case EV_IDLE:
            snprintf(info, sizeof info, "both librarians idle until t=%d", e->other);
            break;
        }
        fprintf(out, "t=%-5d %-9s %-40s %s\n", e->time, event_name(e->type), info, transition);
    }
}

/* Map a simulation time to a column of the Gantt bar. */
static int gantt_col(int t, int origin, int span, int cols)
{
    return (int)((long)(t - origin) * cols / span);
}

void report_gantt(FILE *out, const Workload *w, const SimResult *r, const Summary *s)
{
    int origin = s->first_arrival;
    int span = s->elapsed;
    if (span <= 0)
        return;
    /* Integer columns per time unit when the run is short, otherwise squeeze. */
    int cols = span <= GANTT_WIDTH ? span * (GANTT_WIDTH / span) : GANTT_WIDTH;

    /* Extra room so the last axis label can overhang the bar. */
    char *row = malloc((size_t)cols + 16);
    if (!row)
        return;

    fprintf(out, "\n--- Gantt chart (time %d to %d, '.' = idle) ---\n",
            origin, s->last_completion);

    for (int u = 0; u < NUM_SERVICE_UNITS; u++) {
        memset(row, '.', (size_t)cols + 1);
        row[cols + 1] = '\0';
        for (int i = 0; i < r->nseg; i++) {
            const Segment *g = &r->segments[i];
            if (g->unit != u)
                continue;
            int c0 = gantt_col(g->start, origin, span, cols);
            int c1 = gantt_col(g->end, origin, span, cols);
            if (c1 <= c0)
                continue;  /* too short to draw at this scale */
            row[c0] = '|';
            for (int c = c0 + 1; c < c1; c++)
                row[c] = '=';
            const char *id = w->procs[g->proc].id;
            int room = c1 - c0 - 1;
            int len = (int)strlen(id);
            memcpy(row + c0 + 1, id, (size_t)(len < room ? len : room));
            row[c1] = '|';
        }
        fprintf(out, "Librarian %d  %s\n", u + 1, row);
    }

    /* Time axis: label as many segment boundaries as fit without overlap. */
    memset(row, ' ', (size_t)cols + 15);
    row[cols + 15] = '\0';
    int last_end = -2;
    int *marks = malloc((size_t)(r->nseg * 2 + 2) * sizeof *marks);
    int nmarks = 0;
    if (marks) {
        marks[nmarks++] = origin;
        for (int i = 0; i < r->nseg; i++) {
            marks[nmarks++] = r->segments[i].start;
            marks[nmarks++] = r->segments[i].end;
        }
        /* Simple insertion sort - counts are small. */
        for (int i = 1; i < nmarks; i++) {
            int k = marks[i], j = i - 1;
            while (j >= 0 && marks[j] > k) { marks[j + 1] = marks[j]; j--; }
            marks[j + 1] = k;
        }
        for (int i = 0; i < nmarks; i++) {
            if (i > 0 && marks[i] == marks[i - 1])
                continue;
            char lbl[16];
            int len = snprintf(lbl, sizeof lbl, "%d", marks[i]);
            int c = gantt_col(marks[i], origin, span, cols);
            if (c <= last_end + 1)
                continue;
            memcpy(row + c, lbl, (size_t)len);
            last_end = c + len - 1;
        }
        free(marks);
    }
    for (int c = cols + 14; c >= 0 && row[c] == ' '; c--)
        row[c] = '\0';  /* drop trailing padding */
    fprintf(out, "             %s\n", row);
    free(row);

    /* Exact segment list, since the bar may be scaled. */
    for (int u = 0; u < NUM_SERVICE_UNITS; u++) {
        fprintf(out, "Librarian %d:", u + 1);
        for (int i = 0; i < r->nseg; i++)
            if (r->segments[i].unit == u)
                fprintf(out, " [%d-%d %s]", r->segments[i].start, r->segments[i].end,
                        w->procs[r->segments[i].proc].id);
        fprintf(out, "\n");
    }
}

void report_table(FILE *out, const Workload *w, const SimResult *r, const Summary *s)
{
    int *idx = ids_sorted(w);
    if (!idx)
        return;

    fprintf(out, "\n--- Per-reader results ---\n");
    fprintf(out, "%-8s %-11s %4s %4s %4s %6s %6s %5s %5s %5s %5s\n",
            "READER", "CATEGORY", "ARR", "PRI", "SVC", "START", "FINISH",
            "WAIT", "TAT", "RESP", "PRMT");
    for (int k = 0; k < w->n; k++) {
        const Process *p = &w->procs[idx[k]];
        fprintf(out, "%-8s %-11s %4d %4d %4d %6d %6d %5d %5d %5d %5d\n",
                p->id, p->category, p->arrival, p->priority, p->service,
                p->start, p->completion, waiting_time(p), turnaround_time(p),
                response_time(p), p->preemptions);
    }
    free(idx);

    fprintf(out, "\n--- Summary ---\n");
    fprintf(out, "Average waiting time     : %.2f\n", s->avg_waiting);
    fprintf(out, "Average turnaround time  : %.2f\n", s->avg_turnaround);
    fprintf(out, "Average response time    : %.2f\n", s->avg_response);
    fprintf(out, "Average completion time  : %.2f\n", s->avg_completion);
    fprintf(out, "Longest single wait      : %d (%s)\n", s->max_waiting,
            s->max_waiting_proc >= 0 ? w->procs[s->max_waiting_proc].id : "-");
    fprintf(out, "Throughput               : %.4f readers per time unit (%d in %d)\n",
            s->throughput, s->completed, s->elapsed);
    fprintf(out, "Librarian utilization    : %.2f%% (busy %ld of %d x %d)\n",
            s->utilization, s->busy_time, NUM_SERVICE_UNITS, s->elapsed);
    fprintf(out, "Dispatches / preemptions : %d / %d\n", r->dispatches, r->preemptions);
}

/* ------------------------------------------------------------------ */
/* Algorithm comparison                                                */
/* ------------------------------------------------------------------ */

void report_compare_header(FILE *out, const Workload *w)
{
    fprintf(out, "\n=== Algorithm comparison: %d readers, %d librarians, RR quantum %d ===\n",
            w->n, NUM_SERVICE_UNITS, w->quantum);
    fprintf(out, "%-21s %7s %7s %7s %7s %12s %8s %7s %5s\n",
            "ALGORITHM", "AVG_WT", "AVG_TAT", "AVG_RT", "AVG_C", "MAX_WT(who)",
            "THRUPUT", "UTIL%", "PRMT");
}

void report_compare_row(FILE *out, const Workload *w, const Policy *p,
                        const SimResult *r, const Summary *s)
{
    char name[32], worst[32];
    snprintf(name, sizeof name, "%s%s", p->name, p->required ? "" : " *");
    snprintf(worst, sizeof worst, "%d(%s)", s->max_waiting,
             s->max_waiting_proc >= 0 ? w->procs[s->max_waiting_proc].id : "-");
    fprintf(out, "%-21s %7.2f %7.2f %7.2f %7.2f %12s %8.4f %7.2f %5d\n",
            name, s->avg_waiting, s->avg_turnaround, s->avg_response,
            s->avg_completion, worst, s->throughput, s->utilization, r->preemptions);
}

void report_compare_footer(FILE *out)
{
    fprintf(out, "* = optional extra (not required for Smart Library)\n");
}
