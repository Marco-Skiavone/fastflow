
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
			ticks_wait(1000);	// in base alla macchina, può essere disattivato per tempi diversi!
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

void manager(ff_pipeline& pipe) {
	
	bar.arrive_and_wait();
	std::printf("manager started\n");

	
	const svector<ff_node*> nodes = pipe.get_pipeline_nodes();

	while(!managerstop) {
		for(size_t i = 0; i < (nodes.size() - 1); ++i) {		
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

	ff_pipeline pipe;
	pipe.add_stage(&first);
	for(size_t i = 1; i <= nnodes; ++i)
		pipe.add_stage(new Stage(2000 * i), true);
	pipe.add_stage(&last);

	// setta tutte le code a bounded di capacità 10
	// pipe.setXNodeInputQueueLength(10, true);
	
	// lancio il thread manager
	std::thread th(manager, std::ref(pipe));
	
	// eseguo la pipe
    if (pipe.run_and_wait_end() < 0) {
        error("running pipeline\n");
        return -1;
    }	

	std::printf("pipe done\n");	
	th.join();	// it should make the main thread to wait for th termination (I think)
	std::printf("manager done\n");
	return 0;
}
