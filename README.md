# CS-3710 Assignment 1: Smart Library Management (Process Management)

A deterministic discrete-event simulator in C11. Readers are **processes**; the
two librarians are **two identical parallel service units**. Implements the
required **FCFS** and **non-preemptive Priority**, plus PRIORITY_PREEMPTIVE, RR
and SJF as optional extras.

## 1. Group members

| Name | Roll number |
|------|-------------|
| Arav Kapoor | 1020241147 |
| Zohran Ahmed | 1020231751 |

## 2. Selected scenario

**Smart Library Management.**

| OS concept   | Library equivalent                          |
|--------------|---------------------------------------------|
| Process      | Reader needing help at the desk             |
| Service unit | Librarian (2)                               |
| CPU burst    | SERVICE: time the librarian spends with them |
| Priority     | 1 = highest (e.g. faculty before regular)   |
| Category     | REGULAR / FACULTY / RESEARCHER (descriptive only) |
| Ready queue  | Readers waiting at the desk                 |

## 3. Building and running

Needs a C11 compiler and `make`.

```sh
make          # builds ./library_sched
make test     # runs the test suite
make clean
```

Without `make`: `cc -std=c11 -O2 -o library_sched src/*.c`

## 4. Command-line arguments

```
./library_sched INPUT_FILE ALGORITHM [-v]
```

| ALGORITHM             | Status |
|-----------------------|--------|
| `FCFS`                | required |
| `PRIORITY`            | required (non-preemptive) |
| `PRIORITY_PREEMPTIVE` | optional |
| `RR`                  | optional (quantum from line 2 of the input) |
| `SJF`                 | optional (non-preemptive) |
| `ALL`                 | runs every algorithm and prints a comparison table |

Names are case-insensitive. `-v` prints an event trace, Gantt chart and readable
table to **stderr**; stdout always holds only the machine-readable output.
Exit codes: `0` success, `1` bad input, `2` bad command line.

**Input** (Section 12), e.g. `examples/sample.txt`:

```
5
2
R1,0,3,7,REGULAR
R2,1,1,4,FACULTY
R3,2,3,5,REGULAR
R4,3,2,6,RESEARCHER
R5,4,1,3,FACULTY
```

Invalid input (wrong record count, duplicate IDs, missing or non-numeric
fields, negative arrival, non-positive priority/service/quantum) is rejected
with a line-numbered error. Blank lines, extra whitespace and CRLF are allowed.

**Output** (Section 15):

```
$ ./library_sched examples/sample.txt PRIORITY
PROCESS_ID,ARRIVAL,PRIORITY,SERVICE,START,COMPLETION,WAITING,TURNAROUND,RESPONSE
R1,0,3,7,0,7,0,7,0
R2,1,1,4,1,5,0,4,0
R3,2,3,5,8,13,6,11,6
R4,3,2,6,7,13,4,10,4
R5,4,1,3,5,8,1,4,1
SUMMARY
AVERAGE_WAITING,2.20
AVERAGE_TURNAROUND,7.20
AVERAGE_RESPONSE,2.20
THROUGHPUT,0.3846
UTILIZATION,96.15
```

Rows are in lexicographic ID order. Throughput uses 4 decimals, everything else 2.

## 5. Process model

Each reader is a `Process` struct (`src/process.h`): `id`, `arrival`,
`priority`, `service`, `category` from the input, plus runtime fields `state`,
`remaining` (kept across preemptions), `start` (first dispatch), `completion`,
`ready_seq` (RR queue order) and `preemptions`.

```
NEW --arrival--> READY --dispatch--> RUNNING --done--> TERMINATED
                   ^                    |
                   +----preemption------+   (PRIORITY_PREEMPTIVE / RR only)
```

### Architecture

```
src/
  process.[ch]    Process struct, states, input parsing/validation
  scheduler.[ch]  One comparator per algorithm + flags
  simulator.[ch]  Event loop shared by all algorithms
  metrics.[ch]    Section 10 metrics
  report.[ch]     Machine output, trace, Gantt, comparison
  main.c          CLI
tests/            inputs/, expected/, invalid/, run_tests.sh
examples/         Demo workloads
```

An algorithm is just a **policy**: a ready-queue comparator plus two flags
(`preemptive`, `time_sliced`). All algorithms share one event loop, so any
difference in results comes from the policy alone.

## 6. Scheduling algorithms

**Event loop.** The clock jumps to the next event (arrival, completion or RR
quantum expiry), skipping idle gaps in one step. At each time `t`:

1. Completions free their librarian.
2. Arrivals enter READY in (arrival, ID) order.
3. RR only: expired quanta go to the back of the queue.
4. Each free librarian (lowest index first) takes the best ready reader.
5. PRIORITY_PREEMPTIVE only: preemption check.

**FCFS (required).** Earliest arrival, then smaller ID. Non-preemptive.

**PRIORITY (required).** Smaller priority value, then earlier arrival, then
smaller ID. Non-preemptive: a faculty member arriving mid-service waits for the
next free librarian. Only the numeric PRIORITY is used, never CATEGORY.

**Optional extras:**

* **PRIORITY_PREEMPTIVE.** If the best ready reader has a *strictly* smaller
  priority value than the worst running one (lowest priority, then later
  arrival, then larger ID), the running one is preempted with its remaining
  time kept. Repeats until no such pair exists.
* **RR.** FIFO queue, quantum Q. Arrivals at `t` are queued before a reader
  whose quantum expired at `t`; simultaneous expiries re-queue in (arrival, ID)
  order. A reader finishing exactly at quantum end just completes.
* **SJF.** Shorter service, then earlier arrival, then smaller ID. Non-preemptive.

## 7. Design decisions and assumptions

* **Determinism.** Every comparator ends with an ID tie-break; no randomness or
  wall-clock time. Tests confirm repeated runs are identical.
* **Same-instant order:** completions → arrivals → quantum expiry → dispatch →
  preemption, so a librarian freed at `t` can serve a reader arriving at `t`.
* **Librarian choice** (lowest index when several are free) only affects the
  Gantt chart, not metrics.
* **IDs sort lexicographically**, so `R10` comes before `R2`.
* **Elapsed time E** = last completion − first arrival, the same window the spec
  uses for throughput.
* **Busy time B** is summed from actual execution segments.
* **Ready queue** is an unsorted array with an O(n) scan per dispatch: simple,
  and fast enough for classroom inputs.
* The RR quantum is always read and validated but only RR uses it.

## 8. Metrics

A = arrival, S = first start, C = completion, B = service time.

| Metric | Formula |
|--------|---------|
| Completion | C |
| Turnaround | TAT = C − A |
| Response | RT = S − A (first service) |
| Waiting | WT = TAT − B (all time spent READY) |
| Throughput | N / (last completion − first arrival) |
| Utilization | B_total / (2 × E) × 100 % |

The verbose and `ALL` views also show average completion time, the longest
single wait (a starvation indicator), and dispatch/preemption counts.

## 9. Algorithm comparison and observations

All numbers are real output of `./library_sched FILE ALL`. `*` = optional extra.

### 9.1 Spec sample (`examples/sample.txt`)

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     2.60    7.60    2.60    9.60        6(R5)   0.3846   96.15     0
PRIORITY                 2.20    7.20    2.20    9.20        6(R3)   0.3846   96.15     0
PRIORITY_PREEMPTIVE *    2.60    7.60    1.60    9.60        8(R3)   0.3333   83.33     2
RR *                     4.20    9.20    0.60   11.20        5(R1)   0.3571   89.29     9
SJF *                    2.20    7.20    2.20    9.20        5(R3)   0.3571   89.29     0
```

```
FCFS      Librarian 1  |R1=========================|R4=====================|
          Librarian 2  ....|R2=============|R3=================|R5=========|
                       0   1               5       7           10          13
PRIORITY  Librarian 1  |R1=========================|R4=====================|
          Librarian 2  ....|R2=============|R5=========|R3=================|
                       0   1               5       7   8                   13
```

The required algorithms differ at one point: at t = 5, FCFS serves R3 (arrived
earlier) and PRIORITY serves R5 (faculty). R5's wait drops 6 → 1, R3's rises
3 → 6, and the average falls 2.60 → 2.20. Both finish at t = 13, so throughput
and utilization match: order changes *who* waits, not total work done.

### 9.2 Busy morning, 12 readers (`examples/busy_morning.txt`)

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     5.58    9.67    5.58   14.25      12(R12)   0.4800   98.00     0
PRIORITY                 6.00   10.08    6.00   14.67      14(R05)   0.4444   90.74     0
PRIORITY_PREEMPTIVE *    7.25   11.33    4.92   15.92      15(R02)   0.4444   90.74     4
RR *                     6.17   10.25    3.25   14.83      13(R03)   0.4615   94.23     8
SJF *                    3.33    7.42    3.33   12.00      17(R03)   0.4615   94.23     0
```

| Category   | FCFS avg / max WT | PRIORITY avg / max WT |
|------------|-------------------|-----------------------|
| FACULTY    | 6.00 / 8          | **1.00 / 2**          |
| RESEARCHER | 7.00 / 12         | 6.33 / 8              |
| REGULAR    | 4.67 / 9          | **8.33 / 14**         |

* **Priority doesn't minimise average wait.** It is worse than FCFS here
  (6.00 vs 5.58); it shifts waiting from faculty (6× less) to regular readers
  (almost 2× more).
* PRIORITY leaves regular reader R11 to the end, so librarian 1 idles from
  t = 22 while librarian 2 works until t = 27. Makespan, throughput and
  utilization all suffer (90.7% vs 98.0%): with two units, order affects load
  balance.
* SJF has the lowest average wait but the worst single wait (R03: 17).

### 9.3 Starvation (`tests/inputs/starvation.txt`)

One regular reader R1 at t = 0 and a steady stream of faculty (F1–F6).

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     2.29    6.29    2.29    9.14        4(R1)   0.4375   87.50     0
PRIORITY                 2.29    6.29    2.29    9.14       12(R1)   0.4375   87.50     0
```

Identical averages, but R1 waits 4 under FCFS and 12 under PRIORITY, since a
faculty member is always ready when a librarian frees up. A longer stream would
starve R1 completely. Averages hide this, which is why the longest single wait
is reported. Aging would fix it but would change the specified PRIORITY rules.

### 9.4 Long high-priority jobs (`tests/inputs/priority_long_jobs.txt`)

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     2.67    8.00    2.67    9.00        4(R3)   0.3750  100.00     0
PRIORITY                 5.67   11.00    5.67   12.00       14(R3)   0.3750  100.00     0
```

Under PRIORITY two 1-unit regular jobs wait 14 behind two 10-unit faculty jobs
(convoy effect), more than doubling the average wait.

### 9.5 Summary

| | FCFS | PRIORITY |
|---|---|---|
| Order | arrival | numeric priority |
| Avg WT / TAT | workload-dependent: better in 9.2 and 9.4, worse in 9.1 | lower for high priority, higher for low |
| Fairness | no starvation | low priority can starve |
| Throughput / utilization | equal when makespan matches | can drop if one librarian finishes early |

For non-preemptive algorithms response time equals waiting time. Preemption
(PRIORITY_PREEMPTIVE, RR) lowers response time (RR: 0.60 on the sample) but
raises waiting and turnaround time and adds context switches.

## 10. Testing

`make test` → `passed: 161  failed: 0`. `tests/run_tests.sh` checks:

1. **18 hand-computed expected outputs** covering the spec sample, a single
   reader, simultaneous arrivals, long idle gaps, identical readers, starvation,
   convoy, RR quantum edge cases, and preemption on arrival.
2. **7 malformed inputs** and an unknown algorithm are rejected.
3. **Every input × every algorithm** is deterministic, has N rows, and satisfies
   `TAT = C − A`, `WT = TAT − B`, `0 ≤ RT ≤ WT`, `START ≥ ARRIVAL`.

Also run clean under AddressSanitizer and UndefinedBehaviorSanitizer.

## 11. Known limitations

* Two librarians are fixed by `NUM_SERVICE_UNITS` in `src/simulator.h` (the
  engine supports M units if changed).
* Times are `int`; IDs and categories max 31 characters.
* Ready queue is O(n²) overall; fine for assignment-sized inputs.
* No aging, so PRIORITY can starve low-priority readers (by design, see 9.3).
* Context-switch time is zero.
* The Gantt bar is scaled to 64 columns, so tiny segments may not show; the
  segment list printed below it is always complete.

## 12. Optional features implemented

* Extra algorithms: PRIORITY_PREEMPTIVE, RR, SJF (tested).
* `ALL` mode: side-by-side comparison incl. longest wait and preemption count.
* `-v` mode: event trace of every state transition, ASCII Gantt chart, readable
  results table.
* Context-switch counting.
* Input validation with line-numbered errors.
