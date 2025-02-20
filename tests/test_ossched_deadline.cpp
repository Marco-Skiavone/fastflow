#include <string>
#include <iostream>
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

std::barrier bar{2};
std::atomic_bool managerstop{false};

struct Source: ff_node_t<long> {
	// EB2MS: inserire nel costruttore un parametro n_threads che dice quanti nodi sono. Questo serve per dividere la banda equamente fra tutti
    Source(const int ntasks):ntasks(ntasks) {}

	// EB2MS: per bloccare correttamente i nodi prima che il manager abbia impostato gli sched_attrs
	int svc_init() {
		// EB2MS: si dovrebbe impostare sched_setattr qui con PID=0 (me stesso) per TUTTI i thread
		// parametri budget/deadline/period di default divisi equamente fra gli n_threads
		// ovvero budget proporzionale a 1/n_threads
		bar.arrive_and_wait();        
		// EB2MS: qui prendere il tempo iniziale
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
		// EB2MS: qui prendere il tempo finale e fare differenza con tempo iniziale
		std::printf("Sink finished\n");
		managerstop = true;
	}
};

void manager(ff_farm& farm) {
// EB2MS: il manager dovrebbe soltanto SPOSTARE CPU bandwidth da un thread all'altro, mantenendo una tabellina sugli spostamenti
	bar.arrive_and_wait();
	std::printf("manager started\n");

	svector<ff_node*> nodes = farm.getWorkers();
	ff_farm::lb_t * lb = farm.getlb();
	ff_farm::gt_t * gt = farm.getgt();

    std::printf("0.\n");
	while(!managerstop) {
        std::printf("lb: completed_tasks:%ld\n", lb->getnumtask());
        for(size_t i = 0; i < nodes.size(); ++i) {
            // has access to the counter of completed tasks. Enabled by TRACE_FASTFLOW variables 
            std::printf("node%ld completed_tasks:%ld\n", i + 1, nodes[i]->getnumtask());    
		        // following lines are the previous method to see the output queues of the worker 
                // NOT working for first and last nodes of the farm!
                /* svector<ff_node*> in;		
                nodes[i]->get_out_nodes(in);
                std::printf("node%ld qlen=%ld\n", i + 1, in[0]->get_out_buffer()->length()); */
		}
        std::printf("gt: completed_tasks:%ld\n", gt->getnumtask());
		std::printf("-------\n");
	}
	std::printf("manager completed\n");
}

int main(int argc, char* argv[]) {

    // EB2MS: WARNING fare bene attenzione a dove si mette la lettura di clock_gettime perche' deve essere presa dallo sblocco dell'emettitore alla fine del collettore (svc_end del collettore)
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

	// EB2MS: qui si abilita blocking con condition dei pthread con timeout. Sarebbe da rimuovere perche' vogliamo l'attesa attiva pura
	// che poi venga ridotta dal manager
    farm.blocking_mode();

    // #### starting the farm ###
    // it should allow us to set the policy after that
    // EB2MS: NO, il freeze non serve, serve solo a freezare alla fine, quindi sostituire con run_and_wait_end()
    
    if (farm.run_then_freeze() < 0) {
        error("running then freezing farm\n");
        return -1;
    }

    // setting the SCHED_DEADLINE policy
    // EB2MS: ci pare di capire che sotto stai assegnando SCHED_DEADLINE solo a uno. Perche'?
    // Se per fare una prova, va bene. In generale, meglio impostare SCHED_DEADLINE in svc_init
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
    //farm.ffStats(std::cout);  // It prints out some stats about the farm, for emitter, collector and every worker 
	
	th.join();	// it should make the main thread to wait for th termination
	std::printf("manager done\n");
	return 0;
}
