/*
 * simulator.c - The discrete-event engine.
 *
 * The clock jumps from one event time to the next; no wall-clock waiting.
 * At every event time t the engine performs these steps, in this order:
 *
 *   1. Completions   - readers whose remaining time hit 0 leave (TERMINATED)
 *                      and free their librarian.
 *   2. Arrivals      - readers with arrival == t join the ready queue (READY),
 *                      in (arrival, ID) order.
 *   3. Quantum ends  - (RR only) readers whose quantum expired go to the back
 *                      of the ready queue. Doing this after step 2 implements
 *                      "new arrivals are queued before the preempted process".
 *   4. Dispatch      - every free librarian takes the best ready reader
 *                      according to the policy (READY -> RUNNING).
 *   5. Preemption    - (PRIORITY_PREEMPTIVE only) while some ready reader has
 *                      strictly higher priority than the worst running one,
 *                      swap them (RUNNING -> READY for the victim).
 *
 * The next event time is the earliest of: next arrival, next completion,
 * next quantum expiry. Because all orderings have a unique ID tie-break, the
 * same input and algorithm always yield exactly the same schedule.
 */
#include "simulator.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int proc;          /* index of the reader being served, -1 if idle */
    int slice_start;   /* when the current segment began */
    int quantum_left;  /* RR only: time left in the current quantum */
} ServiceUnit;

typedef struct {
    Workload     *w;
    const Policy *policy;
    SimResult    *r;
    ServiceUnit   units[NUM_SERVICE_UNITS];
    int          *ready;        /* indices of READY processes (unordered) */
    int           nready;
    long          next_seq;     /* source of Process.ready_seq */
    int           oom;          /* set if any allocation failed */
} Sim;

/* ------------------------------------------------------------------ */
/* Logging helpers                                                     */
/* ------------------------------------------------------------------ */

static void log_event(Sim *s, int time, EventType type, int proc, int unit, int other)
{
    SimResult *r = s->r;
    if (r->nev == r->capev) {
        int cap = r->capev ? r->capev * 2 : 64;
        Event *e = realloc(r->events, (size_t)cap * sizeof *e);
        if (!e) { s->oom = 1; return; }
        r->events = e;
        r->capev = cap;
    }
    r->events[r->nev++] = (Event){ time, type, proc, unit, other };
}

/* Close the running segment on librarian `u` at time t. */
static void close_segment(Sim *s, int u, int t)
{
    SimResult *r = s->r;
    ServiceUnit *su = &s->units[u];
    if (t <= su->slice_start)
        return;
    if (r->nseg == r->capseg) {
        int cap = r->capseg ? r->capseg * 2 : 64;
        Segment *g = realloc(r->segments, (size_t)cap * sizeof *g);
        if (!g) { s->oom = 1; return; }
        r->segments = g;
        r->capseg = cap;
    }
    r->segments[r->nseg++] = (Segment){ u, su->proc, su->slice_start, t };
    r->busy_time += t - su->slice_start;
}

/* ------------------------------------------------------------------ */
/* Ready queue                                                         */
/* ------------------------------------------------------------------ */

static void make_ready(Sim *s, int idx)
{
    Process *p = &s->w->procs[idx];
    p->state = STATE_READY;
    p->ready_seq = s->next_seq++;
    s->ready[s->nready++] = idx;
}

/* Position in s->ready of the process the policy would pick next. */
static int best_ready_pos(const Sim *s)
{
    int best = -1;
    for (int i = 0; i < s->nready; i++) {
        if (best < 0 || s->policy->compare(&s->w->procs[s->ready[i]],
                                           &s->w->procs[s->ready[best]]) < 0)
            best = i;
    }
    return best;
}

static int take_ready(Sim *s, int pos)
{
    int idx = s->ready[pos];
    s->ready[pos] = s->ready[--s->nready];
    return idx;
}

/* ------------------------------------------------------------------ */
/* State transitions                                                   */
/* ------------------------------------------------------------------ */

/* READY -> RUNNING on librarian u. */
static void dispatch(Sim *s, int u, int idx, int t)
{
    Process *p = &s->w->procs[idx];
    ServiceUnit *su = &s->units[u];

    su->proc = idx;
    su->slice_start = t;
    su->quantum_left = s->w->quantum;
    p->state = STATE_RUNNING;
    s->r->dispatches++;

    if (p->start < 0) {
        p->start = t;
        log_event(s, t, EV_START, idx, u, -1);
    } else {
        log_event(s, t, EV_RESUME, idx, u, -1);
    }
}

/* RUNNING -> READY for whoever is on librarian u. Remaining time is kept. */
static void preempt(Sim *s, int u, int t, EventType why, int by)
{
    int idx = s->units[u].proc;
    close_segment(s, u, t);
    s->w->procs[idx].preemptions++;
    s->r->preemptions++;
    log_event(s, t, why, idx, u, by);
    s->units[u].proc = -1;
    make_ready(s, idx);
}

/* RUNNING -> TERMINATED for whoever is on librarian u. */
static void complete(Sim *s, int u, int t)
{
    int idx = s->units[u].proc;
    Process *p = &s->w->procs[idx];
    close_segment(s, u, t);
    p->state = STATE_TERMINATED;
    p->completion = t;
    log_event(s, t, EV_COMPLETE, idx, u, -1);
    s->units[u].proc = -1;
}

/* ------------------------------------------------------------------ */
/* Per-event-time steps                                                */
/* ------------------------------------------------------------------ */

static int handle_completions(Sim *s, int t)
{
    int done = 0;
    for (int u = 0; u < NUM_SERVICE_UNITS; u++) {
        int idx = s->units[u].proc;
        if (idx >= 0 && s->w->procs[idx].remaining == 0) {
            complete(s, u, t);
            done++;
        }
    }
    return done;
}

/* `order` is the arrival order; *next is the first not-yet-arrived entry. */
static void handle_arrivals(Sim *s, const int *order, int *next, int t)
{
    while (*next < s->w->n && s->w->procs[order[*next]].arrival <= t) {
        int idx = order[(*next)++];
        log_event(s, t, EV_ARRIVAL, idx, -1, -1);
        make_ready(s, idx);
    }
}

/* RR: readers whose quantum ran out go to the back of the queue. If both
 * librarians expire together, they are re-queued in (arrival, ID) order. */
static void handle_quantum_expiry(Sim *s, int t)
{
    int expired[NUM_SERVICE_UNITS];
    int n = 0;
    for (int u = 0; u < NUM_SERVICE_UNITS; u++)
        if (s->units[u].proc >= 0 && s->units[u].quantum_left == 0)
            expired[n++] = u;

    /* Insertion sort - at most NUM_SERVICE_UNITS entries. */
    for (int i = 1; i < n; i++) {
        int key = expired[i], j = i - 1;
        while (j >= 0 && compare_arrival(&s->w->procs[s->units[expired[j]].proc],
                                         &s->w->procs[s->units[key].proc]) > 0) {
            expired[j + 1] = expired[j];
            j--;
        }
        expired[j + 1] = key;
    }
    for (int i = 0; i < n; i++)
        preempt(s, expired[i], t, EV_QUANTUM, -1);
}

/* Fill free librarians (lowest index first) with the best ready readers. */
static void dispatch_free_units(Sim *s, int t)
{
    for (int u = 0; u < NUM_SERVICE_UNITS && s->nready > 0; u++)
        if (s->units[u].proc < 0)
            dispatch(s, u, take_ready(s, best_ready_pos(s)), t);
}

/* Section 7.3: preempt the lowest-priority running reader (later arrival,
 * then larger ID on ties) whenever a ready reader has strictly higher
 * priority. Repeats until no such pair exists. */
static void handle_priority_preemption(Sim *s, int t)
{
    while (s->nready > 0) {
        int victim = -1;
        for (int u = 0; u < NUM_SERVICE_UNITS; u++) {
            if (s->units[u].proc < 0)
                continue;
            if (victim < 0 || compare_priority(&s->w->procs[s->units[u].proc],
                                               &s->w->procs[s->units[victim].proc]) > 0)
                victim = u;
        }
        if (victim < 0)
            return;

        int pos = best_ready_pos(s);
        const Process *cand = &s->w->procs[s->ready[pos]];
        const Process *run  = &s->w->procs[s->units[victim].proc];
        if (cand->priority >= run->priority)
            return;  /* equal priority never preempts */

        int idx = take_ready(s, pos);
        preempt(s, victim, t, EV_PREEMPT, idx);
        dispatch(s, victim, idx, t);
    }
}

/* Earliest future time at which something happens: the next arrival, a
 * running reader finishing, or (RR) a quantum running out. */
static int next_event_time(const Sim *s, const int *order, int next, int t)
{
    int nt = (next < s->w->n) ? s->w->procs[order[next]].arrival : INT_MAX;
    for (int u = 0; u < NUM_SERVICE_UNITS; u++) {
        const ServiceUnit *su = &s->units[u];
        if (su->proc < 0)
            continue;
        int until = t + s->w->procs[su->proc].remaining;
        if (s->policy->time_sliced && t + su->quantum_left < until)
            until = t + su->quantum_left;
        if (until < nt)
            nt = until;
    }
    return nt;
}

static int any_unit_busy(const Sim *s)
{
    for (int u = 0; u < NUM_SERVICE_UNITS; u++)
        if (s->units[u].proc >= 0)
            return 1;
    return 0;
}

/* Move the clock from t to nt: running readers consume service time. */
static void advance_clock(Sim *s, int t, int nt)
{
    int dt = nt - t;
    for (int u = 0; u < NUM_SERVICE_UNITS; u++) {
        ServiceUnit *su = &s->units[u];
        if (su->proc < 0)
            continue;
        s->w->procs[su->proc].remaining -= dt;
        su->quantum_left -= dt;
    }
}

/* ------------------------------------------------------------------ */

static int *arrival_order(const Workload *w)
{
    int *order = malloc((size_t)w->n * sizeof *order);
    if (!order)
        return NULL;
    for (int i = 0; i < w->n; i++)
        order[i] = i;
    /* Insertion sort by (arrival, ID): stable and fine for classroom N. */
    for (int i = 1; i < w->n; i++) {
        int key = order[i], j = i - 1;
        while (j >= 0 && compare_arrival(&w->procs[order[j]], &w->procs[key]) > 0) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    return order;
}

int simulate(Workload *w, const Policy *policy, SimResult *r)
{
    memset(r, 0, sizeof *r);
    workload_reset(w);

    Sim s = { .w = w, .policy = policy, .r = r };
    for (int u = 0; u < NUM_SERVICE_UNITS; u++)
        s.units[u].proc = -1;

    int *order = arrival_order(w);
    s.ready = malloc((size_t)w->n * sizeof *s.ready);
    if (!order || !s.ready) {
        free(order);
        free(s.ready);
        return -1;
    }

    int next = 0;        /* next entry of `order` that has not arrived */
    int completed = 0;
    int t = w->procs[order[0]].arrival;

    for (;;) {
        completed += handle_completions(&s, t);
        if (completed == w->n)
            break;

        handle_arrivals(&s, order, &next, t);
        if (policy->time_sliced)
            handle_quantum_expiry(&s, t);
        dispatch_free_units(&s, t);
        if (policy->preemptive)
            handle_priority_preemption(&s, t);

        int nt = next_event_time(&s, order, next, t);
        if (!any_unit_busy(&s))
            log_event(&s, t, EV_IDLE, -1, -1, nt);  /* gap until next arrival */
        advance_clock(&s, t, nt);
        t = nt;
    }

    free(order);
    free(s.ready);
    return s.oom ? -1 : 0;
}

void simresult_free(SimResult *r)
{
    free(r->segments);
    free(r->events);
    memset(r, 0, sizeof *r);
}

const char *event_name(EventType t)
{
    switch (t) {
    case EV_ARRIVAL:  return "ARRIVE";
    case EV_START:    return "START";
    case EV_RESUME:   return "RESUME";
    case EV_PREEMPT:  return "PREEMPT";
    case EV_QUANTUM:  return "QUANTUM";
    case EV_COMPLETE: return "COMPLETE";
    case EV_IDLE:     return "IDLE";
    }
    return "?";
}
