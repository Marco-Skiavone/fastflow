#include <string>
#include <iostream>
#include <thread>
#include <barrier>
#include <atomic>
#include <chrono>

#include <ff/ff.hpp>

using namespace ff;

std::barrier bar{2};
std::atomic_bool managerstop{false};

struct Source: ff_node_t<long> {
    Source(const int ntasks):ntasks(ntasks) {}

	int svc_init() {
		bar.arrive_and_wait();        
		return 0;
	}
	long* svc(long*) {
        for(long i = 1; i <= ntasks; ++i) {
			ticks_wait(1000);
            ff_send_out((long*)i);
        }
        return EOS;
    }

    size_t getTID() {
        return ff_getThreadID();
    }

    const int ntasks;
};
struct Stage: ff_node_t<long> {
	Stage(long workload):workload(workload) {}
    long* svc(long*in) {
		ticks_wait(workload);
        return in;
    }
	long workload;
};
struct Sink: ff_node_t<long> {
    long* svc(long*) {
		ticks_wait(1000);
        ++counter;
        return GO_ON;
    }
    size_t counter = 0;

	void svc_end() {
		std::printf("Sink finished\n");
		managerstop = true;
	}
};

void manager(ff_farm& farm) {
	bar.arrive_and_wait();
	std::printf("manager started\n");

	svector<ff_node*> nodes = farm.getWorkers();

    std::printf("0.\n");
	while(!managerstop) {
		for(size_t i = 0; i < nodes.size(); ++i) {		
			svector<ff_node*> in;		
			nodes[i]->get_out_nodes(in);
			std::printf("node%ld qlen=%ld\n", i + 1, in[0]->get_out_buffer()->length());
		}
		std::printf("-------\n");
	}
	std::printf("manager completed\n");
}

int main(int argc, char* argv[]) {
    // default arguments
    size_t ntasks = 1000;
    size_t nnodes = 2;

    // setting the default values for the sched_attr to be set with SCHED_DEADLINE
    struct sched_attr attr = {0};
    attr.size = sizeof(struct sched_attr);
    attr.sched_flags = 0;
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = 50000;
    attr.sched_deadline = 20000000;
    attr.sched_period = 20000000;
    
    if (argc > 1) {
        if (argc < 3 || argc > 5) {
            error("use: %s ntasks nnodes (runtime) (period/deadline)\n", argv[0]);
            return -1;
        } 
        ntasks = std::stol(argv[1]);
		nnodes = std::stol(argv[2]);
        if(argc >= 4)
		    attr.sched_runtime = std::stol(argv[3]);
        if(argc == 5) {
            long val = std::stol(argv[4]);
            attr.sched_period = val;
            attr.sched_deadline = val;
        }
    }

    Source first(ntasks);
    Sink last;

    // ### Creation of farm, adding nodes ###
	ff_farm farm(false, ntasks, ntasks, false, nnodes + 2, true);
    farm.add_emitter(&first);

    std::vector<ff_node *> w;
    for(size_t i = 0; i < nnodes; ++i) 
        w.push_back(new Stage(2000 * i));
    farm.add_workers(w);

    farm.add_collector(&last);


	// ### launching thread manager ###
	std::thread th(manager, std::ref(farm));

    // #### starting then freezing the farm ###
    // it should allow us to set the policy
    if (farm.run_then_freeze() < 0) {
        error("running then freezing farm\n");
        return -1;
    }

    // setting the SCHED_DEADLINE policy
    if (first.getTID() && set_scheduling_out(&attr, first.getTID()) != 0)
        fprintf(stderr, "Error: %d (%s) - %s\n", errno, strerrorname_np(errno), strerror(errno));
    else
        fprintf(stderr, "SCHED_DEADLINE set for %lu!\n", first.getTID());
    print_thread_attributes(first.getTID());

    // ### it thaws the farm, if frozen ###
    std::printf("farm restart\n");
    if (farm.run_and_wait_end() < 0) {
        perror("running farm\n");
        return -1;
    }

    std::cerr << "DONE, time= " << farm.ffTime() << " (ms)\n";
	
	th.join();	// it should make the main thread to wait for th termination
	std::printf("manager done\n");
	return 0;
}
