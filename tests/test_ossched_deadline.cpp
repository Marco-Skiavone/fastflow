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
    Source(const int ntasks, struct sched_attr * attrs):ntasks(ntasks) {
        attr = attrs;
    }

	int svc_init() {
		bar.arrive_and_wait();
        // setting scheduling 

        fprintf(stderr, "DEB: {size: %u, policy: %u (0 for SCHED_OTHER), flags: %llu, nice: %u, priority: %u, deadline: %llu, period: %llu, runtime: %lld}\n", 
            attr->size, attr->sched_policy, attr->sched_flags, attr->sched_nice, attr->sched_priority, 
            attr->sched_deadline, attr->sched_period, attr->sched_runtime);

        if (set_scheduling_out(attr) < 0) {
            perror("sett_scheduling_out failed");
        }
        print_thread_attributes(ff_getThreadID());
		return 0;
	}
	long* svc(long*) {
        for(long i = 1; i <= ntasks; ++i) {

			ticks_wait(1000);
            ff_send_out((long*)i);
        }
        return EOS;
    }
    const int ntasks;
    struct sched_attr * attr;
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
    size_t ntasks = 10000;
    size_t nnodes = 2;

    struct sched_attr * attr = (struct sched_attr *) malloc(sizeof(struct sched_attr));
    attr->size = sizeof(struct sched_attr);
    attr->sched_nice = 0;
    attr->sched_priority = 0;
    attr->sched_util_max = 0;
    attr->sched_flags = 0;
    attr->sched_policy = SCHED_DEADLINE;
    attr->sched_period = 10000;
    attr->sched_deadline = 10000;
    attr->sched_runtime = 5000; // trying half the size of period and deadline

    if (argc > 1) {
        if (argc < 3 || argc > 5) {
            error("use: %s ntasks nnodes (runtime) ()\n",argv[0]);
            return -1;
        } 
        ntasks    = std::stol(argv[1]);
		nnodes    = std::stol(argv[2]);
        if(argc >= 4)
		    attr->sched_runtime = std::stol(argv[3]);
        if(argc == 5) {
            long val = std::stol(argv[4]);
            attr->sched_period = val;
            attr->sched_deadline = val;
        }
    }

    Source first(ntasks, attr);
    Sink   last;

	ff_farm farm(false, ntasks, ntasks, false, nnodes + 2, true);
    farm.add_emitter(&first);

    std::vector<ff_node *> w;
    for(size_t i = 0; i < nnodes; ++i) 
        w.push_back(new Stage(2000 * i));
    farm.add_workers(w); // add all workers to the farm

    farm.add_collector(&last);

	// lancio il thread manager
	std::thread th(manager, std::ref(farm));
    
    if (farm.run_and_wait_end() < 0) {
        error("running farm\n");
        return -1;
    }
    std::cerr << "DONE, time= " << farm.ffTime() << " (ms)\n";
	
	th.join();	// it should make the main thread to wait for th termination (I think)
	std::printf("manager done\n");
	return 0;
}
