# Notes For Professor Bini

> ### Thesis Target:
> Define a module to centralize the management of a scheduling policy for the Building Blocks (BB).<br>Firstly, focusing on **pipelines** and **farms**.

---
> ### Current Task:
> Create a *manager* that can retrieve all the info about a farm or a pipe.<br>
> **Reminder:** pipe, farm and a2a have different inputs and outputs. 

---

### How to run *pipe* and *farm* tests
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


