/*
 * process.c - Parsing of the standardized input format:
 *
 *     N
 *     Q
 *     ID,ARRIVAL,PRIORITY,SERVICE,CATEGORY
 *     ...
 *
 * Blank lines and trailing whitespace / CRLF line endings are tolerated.
 * Anything else that violates the constraints in Section 12.2 of the spec is
 * reported as an error rather than silently "fixed".
 */
#include "process.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUF 512

/* Strip leading and trailing whitespace in place; returns the new start. */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* Parse a whole string as a base-10 int. Returns 0 on success. */
static int parse_int(const char *s, int *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

/* Read the next non-blank line into buf (trimmed). Returns NULL at EOF. */
static char *next_line(FILE *fp, char *buf, size_t len, int *lineno)
{
    while (fgets(buf, (int)len, fp)) {
        (*lineno)++;
        char *t = trim(buf);
        if (*t != '\0')
            return t;
    }
    return NULL;
}

/* Parse one "ID,ARRIVAL,PRIORITY,SERVICE,CATEGORY" record. */
static int parse_record(char *line, Process *p, int lineno, char *err, size_t errlen)
{
    char *fields[5];
    int nf = 0;
    char *cursor = line;

    /* Split on commas manually (strtok would merge empty fields). */
    while (nf < 5) {
        fields[nf++] = cursor;
        char *comma = strchr(cursor, ',');
        if (!comma)
            break;
        *comma = '\0';
        cursor = comma + 1;
    }
    if (nf != 5 || strchr(cursor, ',') != NULL) {
        snprintf(err, errlen, "line %d: expected 5 comma-separated fields", lineno);
        return -1;
    }
    for (int i = 0; i < 5; i++)
        fields[i] = trim(fields[i]);

    if (fields[0][0] == '\0' || strlen(fields[0]) >= MAX_ID_LEN) {
        snprintf(err, errlen, "line %d: ID must be 1-%d characters", lineno, MAX_ID_LEN - 1);
        return -1;
    }
    if (strlen(fields[4]) >= MAX_CAT_LEN) {
        snprintf(err, errlen, "line %d: CATEGORY longer than %d characters", lineno, MAX_CAT_LEN - 1);
        return -1;
    }
    if (parse_int(fields[1], &p->arrival) || p->arrival < 0) {
        snprintf(err, errlen, "line %d: ARRIVAL must be a non-negative integer", lineno);
        return -1;
    }
    if (parse_int(fields[2], &p->priority) || p->priority <= 0) {
        snprintf(err, errlen, "line %d: PRIORITY must be a positive integer", lineno);
        return -1;
    }
    if (parse_int(fields[3], &p->service) || p->service <= 0) {
        snprintf(err, errlen, "line %d: SERVICE must be a positive integer", lineno);
        return -1;
    }
    strcpy(p->id, fields[0]);
    strcpy(p->category, fields[4]);
    return 0;
}

int workload_load(const char *path, Workload *w, char *err, size_t errlen)
{
    char buf[LINE_BUF];
    int lineno = 0;
    char *line;

    w->n = 0;
    w->quantum = 0;
    w->procs = NULL;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        snprintf(err, errlen, "cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    if (!(line = next_line(fp, buf, sizeof buf, &lineno)) || parse_int(line, &w->n) || w->n < 1) {
        snprintf(err, errlen, "line %d: first line must be N >= 1", lineno);
        goto fail;
    }
    if (!(line = next_line(fp, buf, sizeof buf, &lineno)) || parse_int(line, &w->quantum) || w->quantum < 1) {
        snprintf(err, errlen, "line %d: second line must be the RR quantum Q >= 1", lineno);
        goto fail;
    }

    w->procs = calloc((size_t)w->n, sizeof(Process));
    if (!w->procs) {
        snprintf(err, errlen, "out of memory");
        goto fail;
    }

    for (int i = 0; i < w->n; i++) {
        if (!(line = next_line(fp, buf, sizeof buf, &lineno))) {
            snprintf(err, errlen, "expected %d process records but found only %d", w->n, i);
            goto fail;
        }
        if (parse_record(line, &w->procs[i], lineno, err, errlen))
            goto fail;
        for (int j = 0; j < i; j++) {
            if (strcmp(w->procs[j].id, w->procs[i].id) == 0) {
                snprintf(err, errlen, "line %d: duplicate process ID '%s'", lineno, w->procs[i].id);
                goto fail;
            }
        }
    }
    if ((line = next_line(fp, buf, sizeof buf, &lineno))) {
        snprintf(err, errlen, "line %d: more process records than N=%d", lineno, w->n);
        goto fail;
    }

    fclose(fp);
    workload_reset(w);
    return 0;

fail:
    fclose(fp);
    workload_free(w);
    return -1;
}

void workload_reset(Workload *w)
{
    for (int i = 0; i < w->n; i++) {
        Process *p = &w->procs[i];
        p->state = STATE_NEW;
        p->remaining = p->service;
        p->start = -1;
        p->completion = -1;
        p->ready_seq = 0;
        p->preemptions = 0;
    }
}

void workload_free(Workload *w)
{
    free(w->procs);
    w->procs = NULL;
    w->n = 0;
}

const char *state_name(ProcState s)
{
    switch (s) {
    case STATE_NEW:        return "NEW";
    case STATE_READY:      return "READY";
    case STATE_RUNNING:    return "RUNNING";
    case STATE_TERMINATED: return "TERMINATED";
    }
    return "?";
}
