/* This test has been written by Marco Schiavone during an internship, supervised by the Professor Enrico Bini. */
/** **************************************************************
 * The current test gives a starting line (thanking Massimo Torquati and Gabriele for their collaboartion)
 * from which understand the library structure. Using a pipeline, we can use the override of methods to better specify the code. 
 * Here we are making use of a 3 different structures to set a custom pipeline.
 * 
 *  Source ---> Stage ---> ... ---> Stage ---> Sink 
 * 
 * This test is still NOT setting the deadline scheduling policy.
 * 
 * @note the `svc()` method is the one called when the simulation starts. 
 * The `svc_init()` and the `svc_end()` are used to set up or clean up something for the test purpose.
 */
/** @author: Marco Schiavone */

#include <iostream>
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

/** Runtime OFFSET changer. */
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
    Source(const int ntasks): ntasks(ntasks) {}

	int svc_init() {
        // here you can set a policy
		bar.arrive_and_wait();

		// Here setting the starting time
		if (clock_gettime(CLOCK_TYPE, &start_time))
            std::cerr << "ERROR in [start] clock_gettime!" << std::endl;
		return 0;
	}
	long* svc(long*) {
        for(long i = 1; i <= ntasks; ++i) {
			ticks_wait(1000);	// based on the machine, it can be deactivated for different timespecs!
            ff_send_out((long*)i);
        }
        return EOS;
    }
    const int ntasks;
};

struct Stage: ff_node_t<long> {
	Stage(long workload): workload(workload) {}

	int svc_init() {
        // here you can set a policy
		return 0;
	}

    long* svc(long*in) {
		ticks_wait(workload);
        return in;
    }
	long workload;
};

struct Sink: ff_node_t<long> {

	int svc_init() {
        // here you can set a policy
		return 0;
	}

    long* svc(long*) {
		ticks_wait(1000);
        ++counter;
        return GO_ON;
    }
    
	void svc_end() {
		// Here setting the ending time
		if (clock_gettime(CLOCK_TYPE, &end_time))
            std::cerr << "ERROR in [end] clock_gettime!" << std::endl;

		std::cout << "Sink finished" << std::endl;
		managerstop = true;
	}

    size_t counter = 0;
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
            buffer_log << "," << lengths[i];
        buffer_log << '\n';
	}
    std::cout << "-----\nmanager completed" << std::endl;
    
    // writing on file
    std::ofstream oFile("outOLD.csv", std::ios_base::out | std::ios_base::trunc);
    if (oFile.is_open()) {
        if (oFile.good()) {
            oFile << "abs_time,rel_time";
            for (i = 0; i < nodes.size() - 1; ++i)
                oFile << "," << i;
            oFile << '\n' << buffer_log.str() << std::endl;   // writing the output on file
            buffer_log.clear();
            std::cout << "- Output saved on outOLD.csv" << std::endl;
        } else {
            fprintf(stderr, "[ERROR] Output file in manager gave error!");
        }
        oFile.close();
    }
}

int main(int argc, char* argv[]) {
    // Default arguments
    size_t ntasks = 1000;
    size_t nnodes = 2;
  
    if (argc > 1) {
        if (argc != 3) {
            error("use: %s ntasks nnodes\n", argv[0]);
            return -1;
        } 
        ntasks = std::stol(argv[1]);
		nnodes = std::stol(argv[2]);
        if (nnodes > 6) nnodes = 6;
    }

    // calling constructor (see test_ossched_pipe to see further implementation)
    Source first(ntasks);
    Sink last;

	ff_pipeline pipe;
	pipe.add_stage(&first);             // add the source 
    // adding the stages 
	for(size_t i = 1; i <= nnodes; ++i)
		pipe.add_stage(new Stage(2000 * i), true);
	pipe.add_stage(&last);              // add the sink

	// set all queues with a bounded capacity of 10
	// pipe.setXNodeInputQueueLength(10, true);
	
	// Thread manager start
	std::thread th(manager, std::ref(pipe), nnodes + 2);
    // NOTE: the total number of threads of the simulation is:
    // 1 (Source) + nnodes (Stages) + 1 (Sink) + 1 (manager)
	
	// Pipe execution and termination
    if (pipe.run_and_wait_end() < 0) {
        error("running pipeline\n");
        return -1;
    }	

	std::cout << "pipe done" << std::endl;

	th.join();		            // It makes the main thread to wait for the manager termination
	std::cout << "manager closed" << std::endl;
    // Print the time difference between Source/svc_init() and Sink.svc_end() measurements
    std::cout << "Time used: " << diff_timespec(end_time, start_time) << " s" << std::endl;
	return 0;
}
