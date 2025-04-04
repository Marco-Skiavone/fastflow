#include <iostream>
#include <cmath>
#include <string>
#include <sstream>
#include <thread>
#include <barrier>
#include <atomic>
#include <chrono>

#if !defined(FF_INITIAL_BARRIER)
    // to run this test we need to be sure that the initial barrier is executed
    #define FF_INITIAL_BARRIER
#endif

#if !defined(TRACE_FASTFLOW)
    #define TRACE_FASTFLOW
#endif

#include <ff/ff.hpp>
using namespace ff;

/**  Defined by the user (you) for this test. Currently: 
 *   - CLOCK_MONOTONIC_RAW: 
 *      Similar to CLOCK_MONOTONIC, but provides access to a raw hardware-based 
 *      time that is not subject to frequency adjustments. 
 *      This clock does not count time that the system is suspended. */
#define CLOCK_TYPE (CLOCK_MONOTONIC_RAW)

/** Runtime OFFSET changer. `x` passed is the runtime value to stretch */
#define RUNTIME_FRACTION (20)

#define BANDWIDTH_MIN 0.01

/** Used to set a limit on how much a runtime value can grow */
#define MAX_RUNTIME_OFFSET(x, y) (2 * (x) / (y))

/** Used to set a limit on how much a runtime value can decrease */
#define MIN_RUNTIME_OFFSET(x, y) ((x) / (y) / 2)

/** Record used to save a log line in memory. Pointers will need dynamic allocation. Defined as:
 * - timespec abs_time;
 * - size_t * out;
 * - size_t * runtime; */
typedef struct _mng_record {
    timespec abs_time;
    size_t * out;   /* it will need dynamic allocation for # of nodes */
    size_t * runtime;
} mng_record;

std::barrier bar{2};
std::atomic_bool managerstop{false};
struct timespec start_time;
struct timespec end_time;

/** Used to have an approximation of the simulation lasting time.
 * @param tasks The `ntasks` passed to the simulation
 * @param nodes The `nnodes` passed to the simulation (in the interval [1, 6])
 * @returns a double with the estimated time */
double estimated_time(size_t tasks, unsigned int nodes);

/** Used to wrap the setting of the sched_attr in a function, used by all elements of this test.
 * @param n_threads number of threads in the current simulation (Emitter and Collector included) 
 * @param period_deadline value to set as `period` and `deadline` of the sched attr
 * @param runtime the runtime value to set. If 0, it will be set as `period_deadline/n_threads` */
void set_deadline_attr(size_t n_threads, size_t period_deadline, size_t runtime);

struct Source: ff_node_t<long> {
    Source(const int ntasks, size_t n_threads, size_t period_deadline): ntasks(ntasks) , n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline, 0);
		bar.arrive_and_wait();

		if (clock_gettime(CLOCK_TYPE, &start_time))
            std::cerr << "ERROR in [start] clock_gettime!" << std::endl;

		return 0;
	}
	long* svc(long*) {
        for(long i = 1; i <= ntasks; ++i) {
			ticks_wait(1000);	// in base alla macchina, può essere disattivato per tempi diversi!
            ff_send_out((long*)i);
        }
        return EOS;
    }
    const int ntasks;
    const size_t n_threads;
    const size_t period_deadline;
};

struct Stage: ff_node_t<long> {
	Stage(long workload, size_t n_threads, size_t period_deadline): workload(workload) , n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline, 0);
		return 0;
	}

    long* svc(long*in) {
		ticks_wait(workload);
        return in;
    }
	long workload;
	const size_t n_threads;
	const size_t period_deadline;
};

struct Sink: ff_node_t<long> {
	Sink(size_t n_threads, size_t period_deadline): n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline, 0);
		return 0;
	}

    long* svc(long*) {
		ticks_wait(1000);
        ++counter;
        return GO_ON;
    }
    
	void svc_end() {
		if (clock_gettime(CLOCK_TYPE, &end_time))
            std::cerr << "ERROR in [end] clock_gettime!" << std::endl;

		std::cout << "Sink finished" << std::endl;
		managerstop = true;
	}

    size_t counter = 0;
    const size_t n_threads;
    const size_t period_deadline;
};

void manager(ff_pipeline& pipe, size_t n_threads, size_t period_deadline) {
    // NOTE: n_threads is (nnodes + 2)!
    size_t i, times = 0;
    struct sched_attr attr;
    struct timespec waiter;
    waiter.tv_nsec = 1000000; waiter.tv_sec = 0;
    
    // Assigning a memory size of 20 secs of simulation (a LOT of time) divided by the interval (1 ms)
    size_t n_memory_records = (20 / (waiter.tv_nsec / 1e9));
    // size_t n_memory_records = ((estimated_time(ntasks, n_threads - 2) * 1.5) / (waiter.tv_nsec / 1e9));
    const size_t record_arr_size = sizeof(size_t) * (n_threads-1);

    mng_record * mem_buffer = (mng_record *) malloc (sizeof(mng_record) * n_memory_records);
    if (mem_buffer == NULL) {
        std::cerr << "Error in the MANAGER malloc(1) call!" << std::endl;
        bar.arrive_and_wait();              // To START the simulation!
        return;
    }
    
    for (i = 0; i < n_memory_records - 1; ++i) {
        mem_buffer[i].out = (size_t *) malloc (record_arr_size);
        mem_buffer[i].runtime = (size_t *) malloc (record_arr_size);

        if (mem_buffer[i].out == NULL || mem_buffer[i].runtime == NULL) {
            std::cerr << "Error in the MANAGER malloc(2) call!" << std::endl;
            bar.arrive_and_wait();              // To START the simulation!
    
            if (mem_buffer != NULL) {
                for (size_t j = 0; j < i; ++j) {
                    if (mem_buffer[j].out != NULL)
                        free(mem_buffer[j].out);
                    if (mem_buffer[j].runtime != NULL)
                        free(mem_buffer[j].runtime);
                }
                free(mem_buffer);
            }
            return;
        }
    }
    
    // n_thread == nodes.size()
	FFBUFFER * in_s[n_threads - 1];      // out buffers
    size_t lengths[n_threads - 1];       // lengths of out queues
    size_t rt_table[n_threads - 1];      // runtime_table, updated if set

    // <<<------- START Simulation ------->>>
	bar.arrive_and_wait();
	std::cout << "manager started" << std::endl;
    
    const svector<ff_node*> nodes = pipe.get_pipeline_nodes();

    for (i = 0; i < n_threads - 1; ++i) {
        rt_table[i] = get_sched_attributes(nodes[i]->getOSThreadId(), &attr) ? SIZE_MAX : attr.sched_runtime;
        mem_buffer[0].runtime[i] = rt_table[i];
    }

    // getting out nodes in in_s[] array
    for (i = 0; i < (nodes.size() - 1); ++i) {
        svector<ff_node*> in;
        nodes[i]->get_out_nodes(in);
        in_s[i] = in[0]->get_out_buffer();  
    }

    long diff[n_threads - 1];
    const size_t runtime_offset = period_deadline / n_threads / RUNTIME_FRACTION;
    const size_t runtime_max = period_deadline * (1 - BANDWIDTH_MIN) - runtime_offset;
    const size_t runtime_min = period_deadline * BANDWIDTH_MIN + runtime_offset;

    long min_diff = LONG_MAX;
    size_t min_i, decr_i = 1;

	while(!managerstop && times < n_memory_records) {   // and memory has space
        nanosleep(&waiter, NULL);
        clock_gettime(CLOCK_TYPE, &mem_buffer[times].abs_time);

        // reading lengths
        for (i = 0; i < (n_threads - 1); ++i) {
            lengths[i] = in_s[i]->length();
        }
        
        min_i = SIZE_MAX; 
        for (i = 1; i < n_threads - 1; ++i) {
            diff[i] = lengths[i] - lengths[i-1];
            if (diff[i] < min_diff) {
                min_diff = diff[i];
                min_i = i;
            }
        }

        if (decr_i != min_i && min_i != SIZE_MAX && rt_table[decr_i] >= runtime_min && rt_table[min_i] <= runtime_max) {
            rt_table[decr_i] -= runtime_offset;
            rt_table[min_i] += runtime_offset;
            set_deadline_attr(n_threads, period_deadline, rt_table[decr_i]);
            set_deadline_attr(n_threads, period_deadline, rt_table[min_i]);
        }
        if (++decr_i >= n_threads-1)    // circular iteration, but 0 is excluded
            decr_i = 1;
        for (i = 0; i < n_threads-1; ++i) {
            mem_buffer[times].out[i] = lengths[i];
            mem_buffer[times].runtime[i] = rt_table[i];
        }
        ++times;
	}

    if (times >= n_memory_records) {
        std::cerr << "### Warning! ### Memory ended up after " << 
            ((mem_buffer[n_memory_records-1].abs_time.tv_sec - start_time.tv_sec) + ((mem_buffer[n_memory_records-1].abs_time.tv_nsec - start_time.tv_nsec) / 1e9))
            << "s from the start. End of data collection." << std::endl;
    }

    std::cout << "manager completed.\n-----" << std::endl;
    
    // writing on file
    std::ofstream oFile("outV3.csv", std::ios_base::out | std::ios_base::trunc);
    if (oFile.is_open()) {
        if (oFile.good()) {
            oFile << "abs_time,rel_time";
            for (i = 0; i < n_threads - 1; ++i) { oFile << ",node_#" << i; }
            for (i = 0; i < n_threads - 1; ++i) { oFile << ",runtime_#" << i; }
            oFile << '\n';
            
            size_t j;
            for (i = 0; i < times; ++i) {
                // abs and rel times
                oFile << (mem_buffer[i].abs_time.tv_sec + mem_buffer[i].abs_time.tv_nsec / 1e9) << "," 
                    << ((mem_buffer[i].abs_time.tv_sec - start_time.tv_sec) + ((mem_buffer[i].abs_time.tv_nsec - start_time.tv_nsec) / 1e9));
                // out queues
                for (j = 0; j < n_threads-1; ++j) { oFile << "," << mem_buffer[i].out[j]; }
                // runtimes
                for (j = 0; j < n_threads-1; ++j) { oFile << "," << mem_buffer[i].runtime[j]; }
                oFile << std::endl;
            }
        } else {
            fprintf(stderr, "Output file in manager gave error!");
        }
        oFile.close();
    }
    
    // memory free
    if (mem_buffer != NULL) {
        for (i = 0; i < n_memory_records; ++i) {
            if (mem_buffer[i].out != NULL)
                free(mem_buffer[i].out);
            if (mem_buffer[i].runtime != NULL)
                free(mem_buffer[i].runtime);
        }
        free(mem_buffer);
    }
}

int main(int argc, char* argv[]) {
    // default arguments
    size_t ntasks = 1000;
    size_t nnodes = 3;
    size_t period_deadline = 1000000; // Default at 1M

    if (argc > 1) {
        if (argc < 3 || argc > 4) {
            error("use: %s ntasks nnodes (period/deadline: optional)\n", argv[0]);
            return -1;
        } 
        ntasks = std::stol(argv[1]);
		nnodes = std::stol(argv[2]);
        if (argc > 3) 
            period_deadline = std::stol(argv[3]);
        if (nnodes > 6) nnodes = 6;
    }

    Source first(ntasks, nnodes + 2, period_deadline);
    Sink last(nnodes + 2, period_deadline);

	ff_pipeline pipe;
	pipe.add_stage(&first);
	for(size_t i = 1; i <= nnodes; ++i)
		pipe.add_stage(new Stage(2000 * i, nnodes + 2, period_deadline), true);
	pipe.add_stage(&last);

	// setta tutte le code a bounded di capacità 10
	// pipe.setXNodeInputQueueLength(10, true);
	
	// thread manager launch
	std::thread th(manager, std::ref(pipe), nnodes + 2, period_deadline);
	
	// pipe execution
    if (pipe.run_and_wait_end() < 0) {
        error("running pipeline\n");
        return -1;
    }	

	th.join();		// it makes the main thread to wait for th
	std::cout << "manager closed" << std::endl;
    double tot_time = diff_timespec(&end_time, &start_time);
    std::cout << "Time used: " << tot_time << "s" << std::endl;
    std::cout << "Estimated time= " << estimated_time(ntasks, nnodes) << std::endl;

    // writing on file
    std::ofstream oFile("times3.csv", std::ios_base::app);
    if (oFile.is_open()) {
        if (oFile.good()) {
            oFile << ntasks << ", " << nnodes << ", " << tot_time << std::endl;
        } else {
            fprintf(stderr, "Output file in main gave error!");
        }
        oFile.close();
    }
	return 0;
}

void set_deadline_attr(size_t n_threads, size_t period_deadline, size_t runtime) {
    struct sched_attr attr = {0};
    attr.size = sizeof(struct sched_attr);
    attr.sched_flags = 0;
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_deadline = period_deadline;  // default: 1M
    attr.sched_period   = period_deadline;  // default: 1M
    attr.sched_runtime = runtime != 0 ? runtime : (unsigned long)(period_deadline / n_threads);
    
    if (set_scheduling_out(&attr, ff_getThreadID()))
        std::cerr << "Error: " << strerror(errno) << "(" << strerrorname_np(errno) << ") - (line " <<  __LINE__ << ")" << std::endl;
}

double estimated_time(size_t tasks, unsigned int nodes) {
    double a = 0.0, b = 0.0, c = 0.0, res;
    switch (nodes) {
        case 1:
            a = -1.59191582e-14;
            b = 1.77235389e-06;
            c = -2.29503749e-03;
            res = a * pow(tasks, 2) + b * tasks + c;
            return res < 2 ? res : res * 1.2;
        case 5:
            a = 6.25694484e-13;
            b = 9.54211949e-06;
            c = 9.05242235e-02;
            res = a * pow(tasks, 2) + b * tasks + c;
            return res < 2 ? res : res * 1.2;
        case 6:
            a = -9.45730754e-13;
            b = 1.59905464e-05;
            c = -1.08243978e-01;
            res = a * pow(tasks, 2) + b * tasks + c;
            return res < 1 ? res : res * 1.2;
        case 2: 
            a = 2.55215384e-06;
            b = 1.19909262e-03;
            res = a * tasks + b;
            return res < 5 ? res * 1.6 : res;
        case 3:
            a = 5.38947333e-06;
            b = -5.48761504e-02;
            res = a * tasks + b;
            return res < 5 ? res * 1.4 : res;
        case 4:
            a = 7.16414760e-06;
            b = 5.64360384e-03;
            res = a * tasks + b;
            return res < 5 ? res * 1.5 : res;
        default:
            std::cerr << "Estimated time returned max time found until now, could not retrieve data beacuse nodes=" << nodes << std::endl;
            return 29.2344;
    }
}
