/*
 * main.c - Command-line interface for the Smart Library process scheduler.
 *
 *   library_sched INPUT_FILE ALGORITHM [-v]
 *   library_sched INPUT_FILE ALL       [-v]
 *
 * stdout carries only the standardized machine-readable result (or the
 * comparison table for ALL). With -v the event trace, Gantt chart and a
 * readable table go to stderr, so they never pollute the graded output.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "metrics.h"
#include "process.h"
#include "report.h"
#include "scheduler.h"
#include "simulator.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s INPUT_FILE ALGORITHM [-v]\n"
            "\n"
            "ALGORITHM (required for Smart Library):\n"
            "  FCFS                 First-Come First-Served\n"
            "  PRIORITY             Non-preemptive priority (smaller value = higher)\n"
            "ALGORITHM (optional extras):\n"
            "  PRIORITY_PREEMPTIVE  Preemptive priority\n"
            "  RR                   Round Robin with quantum from the input file\n"
            "  SJF                  Non-preemptive Shortest Job First\n"
            "  ALL                  Run every algorithm and print a comparison table\n"
            "\n"
            "Options:\n"
            "  -v, --verbose        Also print event trace, Gantt chart and a\n"
            "                       readable table on stderr\n",
            prog);
}

/* Simulate one algorithm; fills r and s. Returns 0 on success. */
static int run_one(Workload *w, const Policy *p, SimResult *r, Summary *s)
{
    if (simulate(w, p, r) != 0) {
        fprintf(stderr, "error: simulation ran out of memory\n");
        return -1;
    }
    compute_summary(w, r, s);
    return 0;
}

static void print_verbose(const Workload *w, const Policy *p, const SimResult *r, const Summary *s)
{
    report_header(stderr, w, p);
    report_trace(stderr, w, r);
    report_gantt(stderr, w, r, s);
    report_table(stderr, w, r, s);
}

static int run_single(Workload *w, const Policy *p, int verbose)
{
    SimResult r;
    Summary s;
    if (run_one(w, p, &r, &s))
        return 1;
    if (verbose) {
        print_verbose(w, p, &r, &s);
        fprintf(stderr, "\n--- Machine-readable output (stdout) ---\n");
        fflush(stderr);
    }
    report_machine(stdout, w, &s);
    simresult_free(&r);
    return 0;
}

static int run_all(Workload *w, int verbose)
{
    SimResult r[ALG_COUNT];
    Summary s[ALG_COUNT];

    for (int a = 0; a < ALG_COUNT; a++) {
        const Policy *p = policy_get((Algorithm)a);
        if (run_one(w, p, &r[a], &s[a])) {
            while (--a >= 0)
                simresult_free(&r[a]);
            return 1;
        }
        if (verbose) {
            print_verbose(w, p, &r[a], &s[a]);
            fflush(stderr);
        }
    }

    report_compare_header(stdout, w);
    for (int a = 0; a < ALG_COUNT; a++)
        report_compare_row(stdout, w, policy_get((Algorithm)a), &r[a], &s[a]);
    report_compare_footer(stdout);

    for (int a = 0; a < ALG_COUNT; a++)
        simresult_free(&r[a]);
    return 0;
}

int main(int argc, char **argv)
{
    const char *positional[2];
    int npos = 0, verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (npos < 2) {
            positional[npos++] = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (npos != 2) {
        usage(argv[0]);
        return 2;
    }

    const char *input = positional[0];
    const char *alg = positional[1];
    int compare_all = strcasecmp(alg, "ALL") == 0;
    const Policy *policy = compare_all ? NULL : policy_lookup(alg);
    if (!compare_all && !policy) {
        fprintf(stderr, "error: unknown algorithm '%s'\n\n", alg);
        usage(argv[0]);
        return 2;
    }

    Workload w;
    char err[256];
    if (workload_load(input, &w, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }

    int rc = compare_all ? run_all(&w, verbose) : run_single(&w, policy, verbose);
    workload_free(&w);
    return rc;
}
