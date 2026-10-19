# CS-3710 Assignment 1: Smart Library Management (Process Management)

A deterministic discrete-event simulator, written in C11, for the help desk of a
smart library. Readers are modelled as **processes** and the two librarians as
**two identical parallel service units** (CPUs). The program implements the
scheduling algorithms required for the Smart Library scenario, **FCFS** and
**non-preemptive Priority**, and adds PRIORITY_PREEMPTIVE, RR and SJF as
optional extras for comparison.

## 1. Group members

| Name | Roll number |
|------|-------------|
| Arav Kapoor | 1020241147 |
| Zohran Ahmed | 1020231751 |

## 2. Selected scenario

**Smart Library Management.**

| OS concept        | Library equivalent                                      |
|-------------------|---------------------------------------------------------|
| Process           | A reader who needs help at the desk                     |
| CPU / service unit| A librarian (2 of them)                                 |
| CPU burst         | How long the librarian spends with the reader (SERVICE) |
| Arrival time      | When the reader reaches the desk                        |
| Priority          | Service priority (1 = highest), e.g. faculty before regular readers |
| Category          | REGULAR / FACULTY / RESEARCHER (descriptive only)       |
| Ready queue       | Readers waiting at the desk                             |

## 3. Building and running

Requirements: a C11 compiler (`cc`, `gcc` or `clang`) and `make`. There are no
other dependencies.

```sh
make                 # builds ./library_sched
make test            # runs the regression suite in tests/
make clean
```

Without `make`:

```sh
cc -std=c11 -O2 -o library_sched src/*.c
```

## 4. Command-line arguments

```
./library_sched INPUT_FILE ALGORITHM [-v]
```

| ALGORITHM             | Status                     |
|-----------------------|----------------------------|
| `FCFS`                | **required**               |
| `PRIORITY`            | **required** (non-preemptive) |
| `PRIORITY_PREEMPTIVE` | optional extra             |
| `RR`                  | optional extra (quantum taken from line 2 of the input) |
| `SJF`                 | optional extra (non-preemptive) |
| `ALL`                 | runs every algorithm and prints a comparison table |

Algorithm names are case-insensitive. `-v` / `--verbose` prints an event trace,
a Gantt chart and a readable results table on **stderr**. stdout always holds
only the standardized machine-readable output, so `-v` cannot interfere with
automated tests.

Exit codes: `0` success, `1` bad input file, `2` bad command line.

### Input format (Section 12)

```
N
Q
ID,ARRIVAL,PRIORITY,SERVICE,CATEGORY
...
```

Example (`examples/sample.txt`, the spec's library sample):

```
5
2
R1,0,3,7,REGULAR
R2,1,1,4,FACULTY
R3,2,3,5,REGULAR
R4,3,2,6,RESEARCHER
R5,4,1,3,FACULTY
```

The input is validated. The program rejects a wrong record count, duplicate IDs,
missing fields, non-numeric values, a negative arrival, a non-positive priority,
service time or quantum, and over-long IDs. Blank lines, surrounding whitespace
and CRLF line endings are tolerated.

### Output format (Section 15)

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

Rows are printed in lexicographic process-ID order (`strcmp`). Averages and
utilization use 2 decimal places. Throughput uses 4, because values such as
5/13 would otherwise lose most of their precision.

### Verbose mode (for the demo)

```
$ ./library_sched examples/sample.txt FCFS -v > /dev/null
--- Event trace ---
t=0     ARRIVE    R1 (REGULAR, pri 3, needs 7)             NEW -> READY
t=0     START     R1 on Librarian 1                        READY -> RUNNING
t=1     ARRIVE    R2 (FACULTY, pri 1, needs 4)             NEW -> READY
t=1     START     R2 on Librarian 2                        READY -> RUNNING
...
--- Gantt chart (time 0 to 13, '.' = idle) ---
Librarian 1  |R1=========================|R4=====================|
Librarian 2  ....|R2=============|R3=================|R5=========|
             0   1               5       7           10          13
...
```

## 5. Process model

Each reader is a `Process` struct (`src/process.h`):

| Field | Meaning |
|-------|---------|
| `id`, `arrival`, `priority`, `service`, `category` | read from the input file |
| `state` | `NEW`, `READY`, `RUNNING` or `TERMINATED` |
| `remaining` | service time still owed. It is preserved across preemptions. |
| `start` | first time the reader got a librarian (`-1` until then) |
| `completion` | time the reader left |
| `ready_seq` | order in which the reader (re-)entered the ready queue. RR uses it. |
| `preemptions` | number of times the reader was taken off a librarian |

The lifecycle is explicit, and every transition is logged in the event trace:

```
NEW --arrival--> READY --dispatch--> RUNNING --service done--> TERMINATED
                   ^                    |
                   +----preemption------+   (PRIORITY_PREEMPTIVE / RR only)
```

### Architecture

```
src/
  process.[ch]    Process struct, lifecycle states, input parsing/validation
  scheduler.[ch]  Policy table: one comparator per algorithm + flags
  simulator.[ch]  Discrete-event engine shared by all algorithms
  metrics.[ch]    Per-process and summary metrics (Section 10)
  report.[ch]     Machine-readable output, trace, Gantt, tables, comparison
  main.c          CLI parsing and orchestration
tests/
  inputs/         Edge-case workloads
  expected/       Hand-computed expected outputs (NAME.ALGORITHM.out)
  invalid/        Malformed inputs that must be rejected
  run_tests.sh    Regression + determinism + invariant checks
examples/         Demo workloads
```

The main design choice is that **a scheduling algorithm is just a policy**: a
comparator that orders the ready queue, plus two flags (`preemptive` and
`time_sliced`). All five algorithms run through one event loop. When two
algorithms give different results on the same input, the cause is the policy
and nothing else.

## 6. Scheduling algorithms

### Discrete-event loop (shared by all algorithms)

The clock jumps straight to the next event time. It never waits in real time. At
each event time `t`, the loop runs these steps in order:

1. **Completions.** Readers whose `remaining` has reached 0 become TERMINATED and free their librarian.
2. **Arrivals.** Readers with `arrival == t` enter READY in (arrival, ID) order.
3. **Quantum expiry** (RR only). Readers whose quantum ran out go to the back of the queue.
4. **Dispatch.** Each free librarian, lowest index first, takes the best ready reader under the policy.
5. **Preemption** (PRIORITY_PREEMPTIVE only). See below.

The next event time is the earliest of: the next arrival, a running reader
finishing, or an RR quantum expiring. If no librarian is busy, the clock jumps
over the idle gap to the next arrival. This handles long idle periods in a
single step.

### FCFS (required)

* Ordering: earliest arrival first, then the lexicographically smaller ID.
* Non-preemptive. Once a librarian starts with a reader, they finish together.

### Priority, non-preemptive (required)

* Ordering: smaller priority number first, then earlier arrival, then the smaller ID.
* Non-preemptive. A faculty member who arrives while a regular reader is being
  served waits for the next free librarian. They do not interrupt.
* Scheduling uses only the numeric PRIORITY field. CATEGORY is never interpreted.

### Optional extras

* **PRIORITY_PREEMPTIVE.** Same ordering as PRIORITY. After dispatching, if the
  best ready reader has a *strictly* smaller priority value than the worst
  running reader, the worst running reader is preempted. "Worst" means lowest
  priority, then later arrival, then larger ID (Section 7.3). Its remaining time
  is preserved. The check repeats until no such pair exists. Equal priorities
  never preempt.
* **RR.** FIFO ready queue with quantum Q. Arrivals at time `t` are queued before
  a reader whose quantum expired at `t`. A reader that finishes exactly when its
  quantum ends simply completes. If both librarians' quanta expire together, the
  two readers are re-queued in (arrival, ID) order.
* **SJF.** Non-preemptive. Ordering: shorter service time, then earlier arrival, then the smaller ID.

## 7. Design decisions and assumptions

* **Determinism.** Every comparator ends with an ID tie-break, so two distinct
  readers never compare equal. All loops run in a fixed order and no random
  numbers or wall-clock time are used. The test suite runs every input twice
  under every algorithm and checks the outputs are identical.
* **Event ordering at the same instant:** completions → arrivals → quantum
  expiries → dispatch → preemption. As a result, a librarian freed at `t` can
  immediately serve a reader who arrives at `t`.
* **Librarian choice.** When several librarians are free, the lowest-numbered
  one is used. Librarians are identical, so this affects only the Gantt chart,
  never the metrics.
* **Lexicographic IDs.** The spec asks for lexicographic order, so `R10` sorts
  before `R2`. Zero-padded IDs (`R01`, `R02`, …) avoid surprises.
* **Elapsed time E** for utilization is `last completion − first arrival`. This
  is the same window the spec uses for throughput, so time before the first
  reader arrives is not counted as idle.
* **Busy time B** is measured from the recorded execution segments. It is not
  assumed to equal the sum of service times.
* **Ready queue.** It is an unsorted array, and the best entry is found with an
  O(n) scan for each dispatch, giving O(n²) overall. That is fine for
  classroom-sized inputs and keeps the code easy to follow. A binary heap would
  bring it down to O(n log n).
* The RR quantum is read and validated for every algorithm, because the input
  format always includes it, but only RR uses it.

## 8. Metrics

With A = arrival, S = first start, C = completion and B = service time:

| Metric | Formula |
|--------|---------|
| Completion time | C |
| Turnaround time | TAT = C − A |
| Response time | RT = S − A (first service, even if later preempted) |
| Waiting time | WT = TAT − B (all time spent READY, including after preemptions) |
| Throughput | N / (last completion − first arrival), in readers per time unit |
| Utilization | B_total / (2 × E) × 100 %, with E = last completion − first arrival |

The verbose and comparison views also show the average completion time, the
**longest single wait** (a starvation indicator), and the number of
dispatches and preemptions (context switches).

## 9. Algorithm comparison and observations

All numbers below are actual program output (`./library_sched FILE ALL`).
Rows marked `*` are optional extras included for context.

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

The two required algorithms differ at only one decision point. At t = 5,
librarian 2 is free and R3 (regular, waiting since t=2) and R5 (faculty,
arrived t=4) are both ready. FCFS serves R3, the earlier arrival. PRIORITY
serves R5. R5's wait falls from 6 to 1 and R3's rises from 3 to 6. In this case
the short high-priority job happens to go first, so the average wait also
drops (2.60 → 2.20). Both schedules finish at t = 13, so throughput and
utilization are identical. In non-preemptive scheduling the order of service
changes who waits, but not how much total work gets done.

### 9.2 Busy morning, 12 readers (`examples/busy_morning.txt`)

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     5.58    9.67    5.58   14.25      12(R12)   0.4800   98.00     0
PRIORITY                 6.00   10.08    6.00   14.67      14(R05)   0.4444   90.74     0
PRIORITY_PREEMPTIVE *    7.25   11.33    4.92   15.92      15(R02)   0.4444   90.74     4
RR *                     6.17   10.25    3.25   14.83      13(R03)   0.4615   94.23     8
SJF *                    3.33    7.42    3.33   12.00      17(R03)   0.4615   94.23     0
```

Waiting time per category (computed from the per-reader rows):

| Category   | FCFS avg / max WT | PRIORITY avg / max WT |
|------------|-------------------|-----------------------|
| FACULTY    | 6.00 / 8          | **1.00 / 2**          |
| RESEARCHER | 7.00 / 12         | 6.33 / 8              |
| REGULAR    | 4.67 / 9          | **8.33 / 14**         |

Observations:

* **Priority scheduling does not minimise average waiting time.** Here it is
  *worse* than FCFS overall (6.00 vs 5.58). What it does is move waiting time
  from high-priority readers to low-priority ones: faculty wait 6× less, while
  regular readers wait almost twice as long. That trade-off is the purpose of
  priority scheduling.
* Under PRIORITY the last regular reader (R11) is left for the end. The
  librarians then finish unevenly: librarian 1 goes idle at t = 22 while
  librarian 2 works until t = 27. That is why makespan, throughput and
  utilization are worse than FCFS (27 vs 25 time units, 90.7% vs 98.0%). With
  two service units, the order of service also affects load balance.
* SJF (optional) gives the lowest average waiting time, which is what theory
  predicts. It pays for this with the worst single wait (R03, a long
  researcher job, waits 17), so it has its own starvation problem.

### 9.3 Starvation (`tests/inputs/starvation.txt`)

One regular reader R1 arrives at t = 0, together with a steady stream of
faculty members (F1 to F6), all with the same service time.

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     2.29    6.29    2.29    9.14        4(R1)   0.4375   87.50     0
PRIORITY                 2.29    6.29    2.29    9.14       12(R1)   0.4375   87.50     0
```

The averages are **identical**, but the two schedules are not equally fair. In
FCFS, R1 waits 4. In PRIORITY, R1 waits 12, because whenever a librarian
becomes free a faculty member has already arrived. With a slightly longer
faculty stream R1 would never be served, which is **starvation**. Averages
alone can hide this, so the comparison tool reports the longest single wait
as well. Aging (raising a reader's priority the longer they wait) is the
standard fix. It is listed under limitations because it would change the
specified PRIORITY semantics.

### 9.4 Long high-priority jobs (`tests/inputs/priority_long_jobs.txt`)

Two short regular requests (1 unit each) are queued behind two long faculty
requests (10 units each).

```
ALGORITHM              AVG_WT AVG_TAT  AVG_RT   AVG_C  MAX_WT(who)  THRUPUT   UTIL%  PRMT
FCFS                     2.67    8.00    2.67    9.00        4(R3)   0.3750  100.00     0
PRIORITY                 5.67   11.00    5.67   12.00       14(R3)   0.3750  100.00     0
```

PRIORITY makes the 1-unit jobs wait 14 units behind the 10-unit faculty jobs,
which is a **convoy effect**. The average wait more than doubles. Priority
reflects importance, not job length, so it can be far from optimal on
waiting-time metrics.

### 9.5 Summary

| | FCFS | PRIORITY (non-preemptive) |
|---|---|---|
| Ordering | arrival | importance (numeric priority) |
| Average WT / TAT | depends on the workload. It can beat PRIORITY (9.2, 9.4) or lose to it (9.1) | lower for high-priority readers, higher for low-priority ones |
| Response time | equals waiting time (non-preemptive) | equals waiting time (non-preemptive) |
| Fairness | no starvation. Every reader is eventually served in arrival order | low-priority readers can starve under sustained high-priority load |
| Throughput / utilization | identical whenever total work and makespan match | can be slightly lower when priority order leaves one librarian idle at the end |
| Context switches | n dispatches, no preemption | n dispatches, no preemption |

For non-preemptive algorithms, response time always equals waiting time,
because a reader waits only once before being served to completion. The
preemptive extras (PRIORITY_PREEMPTIVE, RR) break that equality: response time
falls (RR: 0.60 on the sample) but waiting and turnaround time rise, and
preemptions are counted as extra context switches.

## 10. Testing

```
$ make test
passed: 161  failed: 0
```

`tests/run_tests.sh` checks three things:

1. **Expected outputs.** 18 cases in `tests/expected/`, each **computed by hand**
   before being compared with the program. They cover: the spec sample (all
   four of FCFS, PRIORITY, PRIORITY_PREEMPTIVE and RR), a single reader, all
   readers arriving together, long idle gaps (0 → 10 → 100), identical
   arrival/priority/service, starvation, convoy, RR quanta equal to, shorter
   than and longer than the service time, and preemption immediately on arrival
   with equal-priority non-preemption.
2. **Invalid input.** 7 malformed files plus an unknown algorithm must be
   rejected with a non-zero exit code.
3. **Every input × every algorithm.** Two runs must give identical output, there
   must be exactly N rows, and the invariants `TAT = C − A`, `WT = TAT − B`,
   `0 ≤ RT ≤ WT` and `START ≥ ARRIVAL` must hold.

All inputs and algorithms were also run under AddressSanitizer and
UndefinedBehaviorSanitizer with no reports.

## 11. Known limitations

* Two service units are fixed at compile time (`NUM_SERVICE_UNITS` in
  `src/simulator.h`) because the scenario specifies two librarians. The engine
  is written for M units, so changing the constant is enough to support more.
* Times are `int`. Inputs whose times exceed about 2×10⁹ would overflow.
* IDs and categories are limited to 31 characters each.
* The ready queue uses a linear scan. This is fine for the input sizes the
  assignment needs, but O(n²) for very large N.
* No aging, so PRIORITY can starve low-priority readers. This is intentional:
  the spec defines PRIORITY without aging (see 9.3).
* Context-switch time is modelled as zero.
* The Gantt bar is scaled to 64 columns, so very short segments in long runs may
  not be drawn. The exact segment list printed under the bar is always
  complete.

## 12. Optional features implemented

* Extra algorithms: **PRIORITY_PREEMPTIVE**, **RR** and **SJF**, implemented
  to the spec's rules and covered by the tests.
* `ALL` mode: **side-by-side comparison** of every algorithm on one input,
  including the longest single wait and the preemption count.
* `-v` mode: **event trace** that shows every lifecycle transition
  (NEW → READY → RUNNING → TERMINATED, and RUNNING → READY on preemption),
  an **ASCII Gantt chart** for each librarian, and a readable results table
  with per-reader preemption counts.
* **Context-switch counting** (dispatches and preemptions).
* Thorough **input validation** with line-numbered error messages.
