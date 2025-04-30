/* This test has been written by Marco Schiavone during an internship, supervised by the Professor Enrico Bini. */
/** **************************************************************
 * The current test tries to handle the deadline scheduling policy on pipeline threads AT RUNTIME.
 * A manager handles the runtime stages and sets a different runtime, also creating a ".csv" file 
 * at the end of the simulation. 
 * 
 * We are making use of a 3 different structs to set a custom pipeline.
 * 
 *  Source ---> Stage#1 ---> ... ---> Stage#<nnodes> ---> Sink 
 *    ^            ^          |            ^               ^
 *    |            |          |            |               |
 *    +------------+------ manager --------+---------------+
 * 
 * @note the `svc()` method is the one called when the simulation starts. 
 * The `svc_init()` and the `svc_end()` are used to set up or clean up something for the test purpose.
 */
/** @author: Marco Schiavone */

#include <iostream>
#include <cmath>
#include <string>
#include <sstream>
#include <thread>
#include <barrier>
#include <atomic>
#include <chrono>

#if !defined(FF_INITIAL_BARRIER)
    // to run this test we need to be sure that the initial barrier is executed (it should sync all the threads)
    #define FF_INITIAL_BARRIER
#endif

/** Uncomment the following macro and recompile to see how the test behaves without scheduling setting at runtime. */
// #define NO_SCHED_SETTING 1

#include <ff/ff.hpp>
using namespace ff;

/**  Defined by the user (you) for this test. Currently: 
 *   - CLOCK_MONOTONIC_RAW: 
 *      Similar to CLOCK_MONOTONIC, but provides access to a raw hardware-based 
 *      time that is not subject to frequency adjustments. 
 *      This clock does not count time that the system is suspended.
 *  
 * @note This type of clock is currently a bad choice to have a clear idea of the performance. 
 * Simply charging your laptop during the simulation may increase OR DECREASE the time registered.
 * So better look up at something else, if you want to compare some performances based on time! */
#define CLOCK_TYPE (CLOCK_MONOTONIC_RAW)

/** Runtime OFFSET changer. `x` passed is the runtime value to stretch */
#define RUNTIME_FRACTION (20)

/** The minimun percentage of the period/deadline amount at which we will set the runtime to */
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

struct Source: ff_node_t<long> {
    Source(const int ntasks, size_t n_threads, size_t period_deadline): ntasks(ntasks) , n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline, 0, 0);
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

/** The internal node of the pipeline. This can be referred to as a "worker" by the `ff` library. */
struct Stage: ff_node_t<long> {
	Stage(long workload, size_t n_threads, size_t period_deadline): workload(workload) , n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline, 0, 0);
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

/** The ending point of the pipeline. It collects payloads without getting out anything. */
struct Sink: ff_node_t<long> {
	Sink(size_t n_threads, size_t period_deadline): n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline, 0, 0);
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
    
    // Assigning a memory size of 20 secs of simulation (quite a LOT of time) divided by the interval
    size_t n_memory_records = (120 / (waiter.tv_nsec / 1e9));
    // size_t n_memory_records = ((estimated_time(ntasks, n_threads - 2) * 1.5) / (waiter.tv_nsec / 1e9));
    const size_t record_arr_size = sizeof(size_t) * (n_threads-1);

    mng_record * mem_buffer = (mng_record *) malloc (sizeof(mng_record) * n_memory_records);
    if (mem_buffer == NULL) {
        std::cerr << "Error in the MANAGER malloc(1) call!" << std::endl;
        bar.arrive_and_wait();              // To START the simulation!
        return;
    }
    
    for (i = 0; i < n_memory_records; ++i) {
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
    size_t node_tids[n_threads - 1];      // tids of the nodes, updated if set
    long diff[n_threads - 1];            // signed

    // declaring runtime offset (to add or decrease) and limit bounce (max, min).
    const size_t runtime_offset = period_deadline / n_threads / RUNTIME_FRACTION;
    const size_t runtime_min = period_deadline * BANDWIDTH_MIN + runtime_offset;
    const size_t runtime_max = period_deadline * (1 - BANDWIDTH_MIN) - runtime_offset;

    long min_diff, max_diff;
    size_t min_i, max_i; 

    // <<<------- START Simulation ------->>>
	bar.arrive_and_wait();
	std::cout << "manager started" << std::endl;
    
    const svector<ff_node*> nodes = pipe.get_pipeline_nodes();

    // getting first runtimes through system calls
    for (i = 0; i < n_threads - 1; ++i) {
        rt_table[i] = get_sched_attributes(nodes[i]->getOSThreadId(), &attr) ? SIZE_MAX : attr.sched_runtime;
        mem_buffer[0].runtime[i] = rt_table[i];
        node_tids[i] = nodes[i]->getOSThreadId();
    }

    // getting out nodes in in_s[] array
    for (i = 0; i < (nodes.size() - 1); ++i) {
        svector<ff_node*> in;
        nodes[i]->get_out_nodes(in);
        in_s[i] = in[0]->get_out_buffer();  
    }
    // ^^^ we made "[0]" to retrieve 1st node in output list

	while(!managerstop && times < n_memory_records) {   // and memory has space
        nanosleep(&waiter, NULL);
        clock_gettime(CLOCK_TYPE, &mem_buffer[times].abs_time);

        // reading lengths
        for (i = 0; i < (n_threads - 1); ++i) {
            if (in_s[i] == NULL) {
                break;
            }
            lengths[i] = in_s[i]->length();
        }
        if (managerstop) {
            fprintf(stderr, "Received end of simulation during control\n");
            break;
        }

        // getting the busiest and least busy nodes
        min_diff = LONG_MAX; max_diff = LONG_MIN; min_i = SIZE_MAX; max_i = SIZE_MAX; 
        for (i = 1; i < n_threads - 1; ++i) {
            diff[i] = lengths[i] - lengths[i-1];
            if (diff[i] < min_diff) {
                min_diff = diff[i];
                min_i = i;
            }
            if (diff[i] > max_diff) {
                max_diff = diff[i];
                max_i = i;
            }
        }
        // We remove from the one having max difference (has done a lot) 
        // and giving to the one having the min (negative - it has greater input queue than output one)
        if (max_i != min_i && max_i != SIZE_MAX && min_i != SIZE_MAX && rt_table[max_i] >= runtime_min && rt_table[min_i] <= runtime_max) {
            rt_table[max_i] -= runtime_offset;
            rt_table[min_i] += runtime_offset;
            // if NO_SCHED_SETTING is defined, the syscalls will be NOT performed.
#ifndef NO_SCHED_SETTING
            set_deadline_attr(n_threads, period_deadline, rt_table[max_i], node_tids[max_i]);
            set_deadline_attr(n_threads, period_deadline, rt_table[min_i], node_tids[min_i]);
#endif
        }

        // Adding data retrieved to the memory
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

    std::cout << "-----\nmanager completed" << std::endl;
    
    // writing on file
    std::ofstream oFile("outV1.csv", std::ios_base::out | std::ios_base::trunc);
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
            std::cout << "- Output saved on outV1.csv" << std::endl;
        } else {
            fprintf(stderr, "[ERROR] Output file in manager gave error!");
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
	
	// Thread manager launch
	std::thread th(manager, std::ref(pipe), nnodes + 2, period_deadline);
	
	// pipe execution (and termination)
    if (pipe.run_and_wait_end() < 0) {
        error("running pipeline\n");
        return -1;
    }	

	th.join();		// This makes the main thread to wait for the manager
	std::cout << "manager closed" << std::endl;

    double tot_time = diff_timespec(end_time, start_time);
    std::cout << "Time used: " << tot_time << "s" << std::endl;

    // writing on a file to catch times in append, useful to make some future studies on times
    std::ofstream oFile("timesV1.csv", std::ios_base::app);
    if (oFile.is_open()) {
        if (oFile.good()) {
            oFile << ntasks << "," << nnodes << "," << tot_time << std::endl;
            std::cout << "- Time saved on timesV1.csv" << std::endl;
        } else {
            fprintf(stderr, "[ERROR] Output file in main gave error!\n");
        }
        oFile.close();
    }
	return 0;
}
