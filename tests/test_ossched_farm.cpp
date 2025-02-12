
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

	
    /** NOTE: cannot add the emitter, because `in[0]->get_out_buffer()` is `nil`! */
	svector<ff_node*> nodes = farm.getWorkers();
    // nodes.insert(nodes.begin(), farm.getEmitter());
    // nodes.insert(nodes.end(), farm.getCollector());

    std::printf("0.\n");
	while(!managerstop) {
		for(size_t i = 0; i < nodes.size(); ++i) {		
			svector<ff_node*> in;		
			nodes[i]->get_out_nodes(in);

            /*std::printf("p: %p\n", in[0]->get_out_buffer());
            if (!in[0]->get_out_buffer())
                std::printf("nil found at i: %ld\n", i);*/

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

    if (argc > 1) {
        if (argc != 3) {
            error("use: %s ntasks nnodes\n",argv[0]);
            return -1;
        }
        ntasks    = std::stol(argv[1]);
		nnodes    = std::stol(argv[2]);
    }


    Source first(ntasks);
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
