/*
 * process.h - Process (reader) model and workload loading.
 *
 * In the Smart Library scenario every reader who walks up to the help desk is
 * modelled as a process. The two librarians are the "CPUs" (service units)
 * and the time a librarian spends with a reader is the reader's CPU burst.
 */
#ifndef PROCESS_H
#define PROCESS_H

#include <stddef.h>

#define MAX_ID_LEN  32
#define MAX_CAT_LEN 32

/* Process lifecycle: NEW -> READY -> RUNNING -> TERMINATED
 * (RUNNING -> READY on preemption). */
typedef enum {
    STATE_NEW,
    STATE_READY,
    STATE_RUNNING,
    STATE_TERMINATED
} ProcState;

typedef struct {
    /* --- static attributes, read from the input file --- */
    char id[MAX_ID_LEN];        /* reader ID, e.g. "R1" */
    int  arrival;               /* time the reader reaches the desk */
    int  priority;              /* smaller value = higher priority */
    int  service;               /* total service time needed (burst) */
    char category[MAX_CAT_LEN]; /* REGULAR / FACULTY / RESEARCHER ... */

    /* --- runtime bookkeeping, filled in by the simulator --- */
    ProcState state;
    int  remaining;    /* service time still owed */
    int  start;        /* first time it got a librarian, -1 if never */
    int  completion;   /* time it finished, -1 if not finished */
    long ready_seq;    /* order in which it (re)entered the ready queue */
    int  preemptions;  /* how many times it was taken off a librarian */
} Process;

/* Everything read from one input file. */
typedef struct {
    int      n;        /* number of readers */
    int      quantum;  /* Round Robin quantum (line 2 of the input) */
    Process *procs;    /* array of n processes, in file order */
} Workload;

/*
 * Parse the standardized input file at `path` into `w`.
 * Returns 0 on success. On failure returns -1 and writes a human-readable
 * message into `err`.
 */
int  workload_load(const char *path, Workload *w, char *err, size_t errlen);

/* Reset all runtime fields so the same workload can be simulated again. */
void workload_reset(Workload *w);

void workload_free(Workload *w);

const char *state_name(ProcState s);

#endif /* PROCESS_H */
