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
> **Reminder:**<br>Pipe, farm and a2a have different inputs and outputs. 

---

### Updates:
- Just found out that to define `FF_INITIAL_BARRIER` lets us start the threads without giving certain errors (see *tests/test_lb_affinity.cpp* for details).


---

### How to run custom *pipe* and *farm* tests
Compile the files as `make test_ossched_pipe` and `make test_ossched_farm`. <br>
No policy will be applied yet.

### How to run a SCHED_DEADLINE test
1. Compile:
``` bash
make test_ossched_deadline
```
2. **(A)** - Run **(as root)** with *ntast* *nnodes*:
``` bash
sudo ./test_ossched_deadline 1000 4
```
2. **(B)** - Optional run **(as root)**, adding *runtime* and *period*:
``` bash
sudo ./test_ossched_deadline 1000 4 50000 100000
```
