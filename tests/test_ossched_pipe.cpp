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
#define RUNTIME_OFFSET(x) (floor((x) / 20.0))

/** Used to set a limit on how much a runtime value can grow */
#define MAX_RUNTIME_OFFSET(x, y) (2 * (unsigned long)((float)x / y))

/** Used to set a limit on how much a runtime value can decrease */
#define MIN_RUNTIME_OFFSET(x, y) (unsigned long)((float)x / y / 2)

std::barrier bar{2};
std::atomic_bool managerstop{false};
struct timespec start_time;
struct timespec end_time;

/** Used to wrap the setting of the sched_attr in a function, used by all elements of this test.
 * @param n_threads number of threads in the current simulation (Emitter and Collector included) 
 * @param period_deadline value to set as `period` and `deadline` of the sched attr
 * @param runtime the runtime value to set. If 0, it will be set as `period_deadline/n_threads` */
void set_deadline_attr(size_t n_threads, size_t period_deadline, size_t runtime) {
    struct sched_attr attr = {0};
    attr.size = sizeof(struct sched_attr);
    attr.sched_flags = 0;
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_deadline = period_deadline;  // default: 1M
    attr.sched_period   = period_deadline;  // default: 1M
    attr.sched_runtime = runtime != 0 ? runtime : (unsigned long)((float)period_deadline / n_threads);
    
    if (set_scheduling_out(&attr, ff_getThreadID()))
        std::cerr << "Error: " << strerror(errno) << "(" << strerrorname_np(errno) << ") - (line " <<  __LINE__ << ")" << std::endl;
    //print_thread_attributes(ff_getThreadID());
}

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
    size_t i;
	std::stringstream buffer_log;	// Creating a string stream to prepare output
    struct sched_attr attr;
    struct timespec waiter, ts;
    waiter.tv_nsec = 1000000; waiter.tv_sec = 0;
    
	bar.arrive_and_wait();
	std::cout << "manager started" << std::endl;
        
    const svector<ff_node*> nodes = pipe.get_pipeline_nodes();
	svector<ff_node*> in_s[nodes.size() - 1];      // out nodes
	size_t lengths[nodes.size() - 1];              // lengths
    size_t rt_table[nodes.size() - 1];       // runtime_table, updated if set
    for (i = 0; i < nodes.size() - 1; ++i) {
        lengths[i] = 0;
        rt_table[i] = get_sched_attributes(nodes[i]->getOSThreadId(), &attr) ? SIZE_MAX : attr.sched_runtime;
    }

    // getting out nodes in in_s[] array
    for (i = 0; i < (nodes.size() - 1); ++i) {
        nodes[i]->get_out_nodes(in_s[i]);
    }
    
	while(!managerstop) {
        nanosleep(&waiter, NULL);
        clock_gettime(CLOCK_TYPE, &ts);
        // TODO adjust nano secs (divide by 10e9) 
        buffer_log << (double(ts.tv_sec) + ts.tv_nsec / 10e9);
        
        // reading lengths
        for (i = 0; i < (nodes.size() - 1); ++i) {
            lengths[i] = in_s[i][0]->get_out_buffer()->length();    // 1st node in output requires "[0]"
        }
        
        size_t diff[nodes.size() - 1];
        size_t min_diff = SIZE_MAX, max_diff = 0, min_i = SIZE_MAX, max_i = SIZE_MAX; 
        for (i = 1; i < nodes.size() - 1; ++i) {
            diff[i] = lengths[i-1] > lengths[i] ? (lengths[i-1] - lengths[i]) : (lengths[i] - lengths[i-1]);
            if (diff[i] < min_diff) {
                min_diff = diff[i];
                min_i = i;
            }
            if (diff[i] > max_diff) {
                max_diff = diff[i];
                max_i = i;
            }
        }
        if (max_i != min_i && max_i != SIZE_MAX && min_i != SIZE_MAX
            && rt_table[max_i] < MAX_RUNTIME_OFFSET(period_deadline, n_threads) && rt_table[min_i] < MAX_RUNTIME_OFFSET(period_deadline, n_threads)
            && rt_table[max_i] > MIN_RUNTIME_OFFSET(period_deadline, n_threads) && rt_table[min_i] > MIN_RUNTIME_OFFSET(period_deadline, n_threads)) {
            
            set_deadline_attr(n_threads, period_deadline, rt_table[max_i] - RUNTIME_OFFSET(rt_table[max_i]));
            set_deadline_attr(n_threads, period_deadline, rt_table[min_i] + RUNTIME_OFFSET(rt_table[min_i]));
            // modify runtime values stored in "table"
            rt_table[max_i] -= RUNTIME_OFFSET(rt_table[max_i]);
            rt_table[min_i] += RUNTIME_OFFSET(rt_table[min_i]);
            // std::printf("#DEB: runtime decrease for %ld and increased for %ld\n", max_i, min_i); // DEBUG
        }
        
        // just printing (for CSV)
        for(size_t i = 0; i < (nodes.size() - 1); ++i)
            buffer_log << ", " << lengths[i];
        buffer_log << '\n';
	}

    std::cout << "-----\nmanager completed:" << std::endl;
    
    // writing on file
    std::ofstream oFile("out.csv", std::ios_base::out | std::ios_base::trunc);
    if (oFile.is_open()) {
        if (oFile.good()) {
            oFile << "TIME\t";
            for (i = 0; i < nodes.size() - 1; ++i)
                oFile << "\t\t" << i;
            oFile << std::endl;
            oFile << buffer_log.str() << std::endl;   // writing the output on file
            buffer_log.clear();
        } else {
            fprintf(stderr, "Output file in manager gave error!");
        }
        oFile.close();
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

	std::cout << "pipe done" << std::endl;	
	th.join();		// it makes the main thread to wait for th
	std::cout << "manager closed" << std::endl;
    std::cout << "Time used: " << diff_timespec(&end_time, &start_time) << "s" << std::endl;
	return 0;
}
