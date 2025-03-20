# Notes For Professor Bini

### Index:
1. **Thesis Target** & Current Task
2. **Updates** (most recents notes that could be relevant)
3. **How** to run tests

<br>

 ---

<br>

### Thesis Target:
Define a module to centralize the management of a scheduling policy for the Building Blocks (BB). Firstly, we are focusing on **pipelines** and **farms**.
<br>

### Current Task:
Create a *manager* that can retrieve all the info about a farm or a pipe.<br>
> **Reminder:**<br>Pipe, farm and a2a have different inputs and outputs. I will work with pipelines, for now.

---

### Updates:
Testing if we can move usage time from a node to another. It seems to work.
- Firstly, we added a clock (`CLOCK_MONOTONIC_RAW`) to see time before and after the simulation
- Then we output to a stream, to avoid a slowdown due to printing statements.
- We calculate the differences between *in* and *out* of the nodes, to change the runtime values.
- Finally we print to a .csv file (`out.csv`) the stream buffer.

> **Note:**<br>
> The file `tests/test_ossched_pipeOLD.cpp` runs the simulation **without attribute adjustments**.<br> 
> It can be used to compare times of the simulation before and after this updates.


#### <<<----- Previous Updates ----->>>

> There is a function that prints on a stream some stats about the gain of each node. It retrieves:<br>
>   - work-time (ms)
>   - n. of **tasks**
>   - svc ticks (+ min ticks & max ticks)
>   - n. of **push lost** (+ ticks of pushes lost)
>   - n. of **pop lost** (+ ticks of pops lost)

#### TODO:<br>Maybe to discuss with Professor Torquati to see if we really can make the manager gather those stats as valid data. 
- To define `TRACE_FASTFLOW` lets us use some extra variables in **node.hpp** that count ticks and # of task completed. 
- Just found out that to define `FF_INITIAL_BARRIER` lets us start the threads without giving certain errors (see *tests/test_lb_affinity.cpp* for details).


---

### How to run custom *pipe* and *farm* tests
1. Compile the files as `make test_ossched_pipe` and `make test_ossched_farm`.

2. Run **(as root)** with *ntasks* *nnodes* and *period/runtimes* (default: 1000 3 1M)
``` bash
sudo ./test_ossched_pipe 1000 6 10000000
```

### How to run a SCHED_DEADLINE test
At the beginning of the internship, the purpose of this file was to understand how to set up the desired scheduling policy (`SCHED_DEADLINE`).

1. Compile:
``` bash
make test_ossched_deadline
```
2. **(A)** - Run **(as root)** with *ntasks* *nnodes*:
``` bash
sudo ./test_ossched_deadline 1000 4
```
2. **(B)** - Optional run **(as root)**, adding *runtime* and *period*:
``` bash
sudo ./test_ossched_deadline 1000 4 50000 100000
```
