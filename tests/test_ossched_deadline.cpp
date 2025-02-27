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
struct timespec start_time;
struct timespec end_time;


struct Source: ff_node_t<long> {
	// EB2MS: inserire nel costruttore un parametro n_threads che dice quanti nodi sono. Questo serve per dividere la banda equamente fra tutti
    Source(const int ntasks, size_t n_threads): ntasks(ntasks) , n_threads(n_threads) {}

	// EB2MS: per bloccare correttamente i nodi prima che il manager abbia impostato gli sched_attrs
	int svc_init() {
		// EB2MS: si dovrebbe impostare sched_setattr qui con PID=0 (me stesso) per TUTTI i thread
		// parametri budget/deadline/period di default divisi equamente fra gli n_threads
		// ovvero budget proporzionale a 1/n_threads
        struct sched_attr attr = {0};
        attr.size = sizeof(struct sched_attr);
        attr.sched_flags = 0;
        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_deadline = 1000000L;
        attr.sched_period   = 1000000L;   // 1M 
        attr.sched_runtime = (long long unsigned int)((float)attr.sched_deadline / n_threads);
        
        if (set_scheduling_out(&attr, ff_getThreadID()))
            fprintf(stderr, "Error: %s (%s) - (line %d)\n", strerror(errno), strerrorname_np(errno), __LINE__);
        print_thread_attributes(ff_getThreadID());

        // --- End of schedule setting ---
		bar.arrive_and_wait();        

		// EB2MS: qui prendere il tempo iniziale
        if (clock_gettime(CLOCK_REALTIME, &start_time))
            fprintf(stderr, "ERROR in [start] clock_gettime()!\n");
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
    const size_t n_threads;
};

struct Stage: ff_node_t<long> {
	Stage(long workload, size_t n_threads): workload(workload), n_threads(n_threads) {}

    int svc_init() {
        struct sched_attr attr = {0};
        attr.size = sizeof(struct sched_attr);
        attr.sched_flags = 0;
        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_deadline = 1000000L;
        attr.sched_period   = 1000000L;   // 1M 
        attr.sched_runtime = (long long unsigned int)((float)attr.sched_deadline / n_threads);
        
        if (set_scheduling_out(&attr, ff_getThreadID()))
            fprintf(stderr, "Error: %s (%s) - (line %d)\n", strerror(errno), strerrorname_np(errno), __LINE__);
        print_thread_attributes(ff_getThreadID());
        return 0;
    }
  
    long* svc(long*in) {
		ticks_wait(workload);
        return in;
    }

	long workload;
    const size_t n_threads;
};

struct Sink: ff_node_t<long> {
    Sink(size_t n_threads): n_threads(n_threads) {}

    int svc_init() {
        struct sched_attr attr = {0};
        attr.size = sizeof(struct sched_attr);
        attr.sched_flags = 0;
        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_deadline = 1000000L;
        attr.sched_period   = 1000000L;   // 1M 
        attr.sched_runtime = (long long unsigned int)((float)attr.sched_deadline / n_threads);
        
        if (set_scheduling_out(&attr, ff_getThreadID()))
            fprintf(stderr, "Error: %s (%s) - (line %d)\n", strerror(errno), strerrorname_np(errno), __LINE__);
        print_thread_attributes(ff_getThreadID());
        return 0;
    }

    long* svc(long*) {
		ticks_wait(1000);
        ++counter;
        return GO_ON;
    }
    size_t counter = 0;

	void svc_end() {
		// EB2MS: qui prendere il tempo finale e fare differenza con tempo iniziale
        if (clock_gettime(CLOCK_REALTIME, &end_time))
            fprintf(stderr, "ERROR in [end] clock_gettime()!\n");
		std::printf("Sink finished\n");
		managerstop = true;
	}

    const size_t n_threads;
};

void manager(ff_farm& farm) {
    // EB2MS: il manager dovrebbe soltanto SPOSTARE CPU bandwidth da un thread all'altro, mantenendo una tabellina sugli spostamenti
	bar.arrive_and_wait();
	std::printf("manager started\n");
	
    svector<ff_node*> nodes = farm.getWorkers();
	ff_farm::lb_t * lb = farm.getlb();
	ff_farm::gt_t * gt = farm.getgt();

    std::printf("-------\n");
	while(!managerstop) {
        std::printf("lb: completed_tasks:%ld\n", lb->getnumtask());
        for(size_t i = 0; i < nodes.size(); ++i) {
            // has access to the counter of completed tasks. Enabled by TRACE_FASTFLOW variables 
            std::printf("node%ld completed_tasks:%ld\n", i + 1, nodes[i]->getnumtask());    
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
  
    if (argc > 1) {
        if (argc != 3) {
            error("use: %s ntasks nnodes (runtime) (period/deadline)\n", argv[0]);
            return -1;
        } 
        ntasks = std::stol(argv[1]);
		nnodes = std::stol(argv[2]);
        if (nnodes > 7) nnodes = 7;
    }

    // ### Creation of farm, adding nodes ###
	ff_farm farm(false, ntasks, ntasks, false, nnodes + 2, true);

    // NOTE: 'nnodes' is the total amount of internal nodes, we're adding 1 emitter and 1 collector
    Source first(ntasks, nnodes+2); 
    farm.add_emitter(&first);

    std::vector<ff_node *> w;
    for(size_t i = 0; i < nnodes; ++i) 
        w.push_back(new Stage(2000 * i, nnodes+2));
    farm.add_workers(w);

    Sink last(nnodes+2);
    farm.add_collector(&last);

	// ### launching thread manager ###
	std::thread th(manager, std::ref(farm));

	// EB2MS: qui si abilita blocking con condition dei pthread con timeout. Sarebbe da rimuovere perche' vogliamo l'attesa attiva pura
	// che poi venga ridotta dal manager // farm.blocking_mode(); // --> Actually removed

    // #### starting the farm ###
    if (farm.run_and_wait_end() < 0) {
        error("running farm\n");
        return -1;
    }

    // Wanting to see if the time measured is the same as the farm.ffTime()
    std::cout << "Time used: " << (end_time.tv_sec - start_time.tv_sec) << "." << (end_time.tv_nsec - start_time.tv_nsec) << " s \n";

    std::cerr << "DONE, time= " << farm.ffTime() << " (ms)\n";
    std::cerr << "--------\n";

    //farm.ffStats(std::cout);  // It prints out some stats about the farm, for emitter, collector and every worker 
	
	th.join();	// it should make the main thread to wait for th termination
	std::printf("manager done\n");
	return 0;
}
