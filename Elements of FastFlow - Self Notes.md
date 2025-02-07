Elements of FastFlow - Self Notes

- **node** $\rightarrow$ unit of the sequential execution, a.k.a. basic "block"

- **node combiner** $\rightarrow$ combines two sequential nodes into one "sequential node". It is the same logic of function composition!

- **pipeline** $\rightarrow$ it is a dual concept: 
    1. CONTAINER of building blocks
    2. BUILDER  of the application topology $\rightarrow$ It builds the blocks togheter, modelling the data-flow execution

- **farm** $\rightarrow$ it models the functional replication of b. blocks (BBs) coordinated by a master. Here how it works.
  There are 2 entities working in parallel:
    - A multi-output master node (called Emitter) - has to emit the data flow, given in input, to a pool of pipelines.
    - A pool of pipelines (called Workers) has the scope of transmitting the data via a default SCHEDULING POLICY, or through a user defined code. [*]

- **all-to-all (A2A)** $\rightarrow$ type of building block where there are 2 sets of workers.
  Each worker of the first set (L-Worker) is linked to every worker of the second set (R-Worker).

<br>
<br>

[*] Not sure of how the **definition** and **insertion** of a policy inside the project directory and through the building tools should work.

---

### Objective
The graph of nodes can be **transformed** to optimize the graph: **REDUCING** the number of nodes and enabling **advanced COMBINATION** of BBs (building Blocks) 

There is a simple interface provided to the user to **automatically** transform the graph: `optimize_static()`. Automatic transformation already implemented are:
- *nodes reduction* $\rightarrow$ *Nesting a farm into another farm or pipeline can result in too many collector.* We can reduce the amount of collectors, removing the outer ones and redirecting the inner collectors towards the output of the outer BB. *This idea comes up in two ways: Top or bottom examples in provided slides (27th page)*

- *farm fusion* $\rightarrow$ *Pipeline of farms **having the same parallelism degree** (number of workers, vertical cardinality of f nodes) can increase latency and collector bottlenecks.* **Merging** two sequential farms can increase the performance. It can be done recursively.

- *farm combine* $\rightarrow$ *Similar to farm fusion but for non-parallel farms*. As seen here:
<br>
<img src="Immagine incollata.png" alt="not found." width="600">

- *All-to-All introduction* $\rightarrow$ *To reduce latency having pipelines of farms*: we can combine using **multi-output** and **multi-input** nodes. As the following image shows:
<br>
<img src="Immagine incollata (2).png" alt="not found." width="600">
