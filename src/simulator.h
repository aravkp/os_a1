/*
 * simulator.h - Deterministic discrete-event simulation of the library desk.
 */
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "process.h"
#include "scheduler.h"

/* Two librarians = two identical parallel service units (Section 8). */
#define NUM_SERVICE_UNITS 2

/* One contiguous stretch of a librarian serving one reader (a Gantt bar). */
typedef struct {
    int unit;   /* librarian index, 0-based */
    int proc;   /* index into Workload.procs */
    int start;
    int end;
} Segment;

typedef enum {
    EV_ARRIVAL,   /* NEW -> READY */
    EV_START,     /* READY -> RUNNING, first time */
    EV_RESUME,    /* READY -> RUNNING, after a preemption */
    EV_PREEMPT,   /* RUNNING -> READY, displaced by a higher priority reader */
    EV_QUANTUM,   /* RUNNING -> READY, RR quantum used up */
    EV_COMPLETE,  /* RUNNING -> TERMINATED */
    EV_IDLE       /* both librarians idle until the next arrival */
} EventType;

/* Entry in the event log, used for the --verbose trace. */
typedef struct {
    int       time;
    EventType type;
    int       proc;   /* -1 for EV_IDLE */
    int       unit;   /* -1 if not tied to a librarian */
    int       other;  /* EV_IDLE: time idling ends; EV_PREEMPT: preempting proc */
} Event;

typedef struct {
    Segment *segments;
    int      nseg, capseg;
    Event   *events;
    int      nev, capev;
    long     busy_time;   /* sum of busy time across all librarians */
    int      dispatches;  /* number of times a reader was given a librarian */
    int      preemptions; /* number of RUNNING -> READY transitions */
} SimResult;

/*
 * Run `policy` on workload `w`. Per-process results (start, completion, ...)
 * are written into w->procs; the timeline is written into `r`.
 * Returns 0 on success, -1 on allocation failure.
 */
int  simulate(Workload *w, const Policy *policy, SimResult *r);

void simresult_free(SimResult *r);

const char *event_name(EventType t);

#endif /* SIMULATOR_H */
