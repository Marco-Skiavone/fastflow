#include <iostream>
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

std::barrier bar{2};
std::atomic_bool managerstop{false};
struct timespec start_time;
struct timespec end_time;

/** Used to wrap the setting of the sched_attr in a function, used by all elements of this test.
 * @param n_threads number of threads in the current simulation (Emitter and Collector included). 
 * @param period_deadline value to set as `period` and `deadline` of the sched attr. 
 * @note The `runtime` attribute will be derived by the other two as . */
void set_deadline_attr(size_t n_threads, size_t period_deadline) {
    struct sched_attr attr = {0};
    attr.size = sizeof(struct sched_attr);
    attr.sched_flags = 0;
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_deadline = period_deadline;  // default: 1M
    attr.sched_period   = period_deadline;  // default: 1M
    attr.sched_runtime = (long long unsigned int)((float)period_deadline / n_threads);
    
    if (set_scheduling_out(&attr, ff_getThreadID()))
        std::cerr << "Error: " << strerror(errno) << "(" << strerrorname_np(errno) << ") - (line " <<  __LINE__ << ")" << std::endl;
    print_thread_attributes(ff_getThreadID());
}

struct Source: ff_node_t<long> {
    Source(const int ntasks, size_t n_threads, size_t period_deadline): ntasks(ntasks) , n_threads(n_threads), period_deadline(period_deadline) {}

	int svc_init() {
		set_deadline_attr(n_threads, period_deadline);
		bar.arrive_and_wait();

		// EB2MS: qui prendere il tempo iniziale
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
		set_deadline_attr(n_threads, period_deadline);
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
		set_deadline_attr(n_threads, period_deadline);
		return 0;
	}

    long* svc(long*) {
		ticks_wait(1000);
        ++counter;
        return GO_ON;
    }
    
	void svc_end() {
		// EB2MS: qui prendere il tempo finale e fare differenza con tempo iniziale
		if (clock_gettime(CLOCK_TYPE, &end_time))
            std::cerr << "ERROR in [end] clock_gettime!" << std::endl;

		std::cout << "Sink finished" << std::endl;
		managerstop = true;
	}

    size_t counter = 0;
    const size_t n_threads;
    const size_t period_deadline;
};

void manager(ff_pipeline& pipe, size_t n_threads) {
    size_t i;
	std::stringstream buffer_log;	// Creating a string stream to prepare output
    struct timespec ts, waiter;
    waiter.tv_nsec = 1000000; // 1M
    waiter.tv_sec = 0;
	bar.arrive_and_wait();
	std::cout << "manager started" << std::endl;
    
	const svector<ff_node*> nodes = pipe.get_pipeline_nodes();
    FFBUFFER * in_s[nodes.size() - 1];      // out buffers
    size_t lengths[nodes.size() - 1];       // lengths

    // getting out nodes in in_s[] array
    for (i = 0; i < (nodes.size() - 1); ++i) {
        svector<ff_node*> in;
        nodes[i]->get_out_nodes(in);
        in_s[i] = in[0]->get_out_buffer();  
    }
    // ^^^ we made "[0]" to retrieve 1st node in output list
	
	while(!managerstop) {
        nanosleep(&waiter, NULL);
        clock_gettime(CLOCK_TYPE, &ts);
        buffer_log << ts.tv_sec << '.' << ts.tv_nsec << ", " 
            << (ts.tv_sec - start_time.tv_sec) << '.' << (ts.tv_nsec - start_time.tv_nsec);

        for (i = 0; i < nodes.size() - 1; ++i)
            lengths[i] = in_s[i]->length();

		// just printing (for CSV) -> become memo setting
        for(size_t i = 0; i < (nodes.size() - 1); ++i)
            buffer_log << ", " << lengths[i];
        buffer_log << '\n';
	}
    std::cout << "-----\nmanager completed:" << std::endl;
    
    // writing on file
    std::ofstream oFile("outOLD.csv", std::ios_base::out | std::ios_base::trunc);
    if (oFile.is_open()) {
        if (oFile.good()) {
            oFile << "abs_time,\t\t\trel_time";
            for (i = 0; i < nodes.size() - 1; ++i)
                oFile << ",\t\t" << i;
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
    size_t nnodes = 2;
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
	
	// lancio il thread manager
	std::thread th(manager, std::ref(pipe), nnodes + 2);
	
	// eseguo la pipe
    if (pipe.run_and_wait_end() < 0) {
        error("running pipeline\n");
        return -1;
    }	

	std::cout << "pipe done" << std::endl;	
	th.join();		// it makes the main thread to wait for th termination
	std::cout << "manager closed" << std::endl;

    std::cout << "Time used: " << diff_timespec(&end_time, &start_time) << " s" << std::endl;
	return 0;
}
