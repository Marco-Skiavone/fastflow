# Evaluation of SCHED_DEADLINE for FastFlow
This work focused on evaluating runtime adjustments (using the **SCHED_DEADLINE** policy) across nodes.  
So far, the results do not appear effective enough to justify implementation.

## Index
1. [Tests performed](#tests-performed)  
2. [Active Branches on the repo](#active-branches-for-different-strategies)
3. [How to run tests](#how-to-run-tests)  
4. [Time considerations](#time-considerations)  

---

## Tests performed
All **tests** are the files starting with `test_ossched_` inside the `tests` folder. The actual header module added to the FastFlow library is `sched_monitor.hpp`, located in the `ff` folder.  
The **main** test file is `test_ossched_pipe.cpp`.


**Modus Operandi:**

- First, we defined 3 elements: **Source**, **Stage**, and **Sink**.  
  They extend `ff_node` and implement the `svc_init()` and `svc()` methods. Additionally, Sink struct implements the `svc_end()`.  
- We then added a clock (`CLOCK_MONOTONIC_RAW`) to measure **time** before and after the simulation.  
- We stored the **output queue lengths** of the nodes — collected over time — into **allocated memory**.  
- We calculated the difference between *output queue lenght* values of the nodes to identify which nodes should change their runtime parameters.  
- Finally, we exported the mapped memory buffer to `.csv` files (`out.csv`, etc).

---

## Active branches for different strategies
Three branches were created to test alternative strategies:  

- In the **master** branch, the manager reallocates runtime values from the least loaded node to the busiest one.  
- In the **V2** branch, the manager takes runtime from a pool of nodes and assigns it to the busiest one.  
- In the **V3** branch, the manager removes runtime from nodes following a circular iteration index.  

These branches should eventually be merged and refactored into a strategy pattern to avoid confusion.

---

## How to run tests
From inside the `tests` folder, run:

```bash
make test_ossched_pipe
sudo make test_ossched_pipe <n_tasks> <n_nodes> <n_period?> 
```

Where:

- Valid `n_tasks` values are in the range [1,000 – 100,000,000].
  More may freeze the machine; fewer may be too fast to observe.
- `n_nodes` depends on the machine architecture (number of processors).
- `n_period` is an optional argument. Please ensure the correct `REL_PATH` is set.
- Default values for `n_tasks`, `n_nodes`, and `period/deadline` are: `1000 3 1M`.

**Note:**

The file `test_ossched_pipeOLD.cpp` runs the simulation with **SCHED_DEADLINE** enabled (equal runtime distribution), but **without attribute adjustments**.
It can be used to compare execution times before and after the updates.

Furthermore, it is possible to compile the test `test_ossched_pipe.cpp` defining `NO_SCHED_SETTING` to run the simulation **without runtime changing**. 

---

## Time Considerations
Execution time measured with `CLOCK_MONOTONIC_RAW` may vary significantly across tests.
Even with the same configuration, results can differ by 30–50%, especially with high node & task numbers.

