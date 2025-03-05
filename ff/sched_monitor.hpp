#ifndef _SCHED_MONITORING_H
#define _SCHED_MONITORING_H 1

/** It seems that for the systems using:
 * - Ubuntu 24.04.1 
 * - Kernel 6.8.0-51-generic) 
 * The following include contains the sched_attr struct required for the scheduling. 
 * Credits to: https://github.com/torvalds/linux/blob/master/include/linux/sched.h */
#include <linux/sched/types.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#define N_PROCESSORS (sysconf(_SC_NPROCESSORS_ONLN))

/** Wrapper function to set a mask of cpu(s) affinity for a process.
 * @param pid The pid of the process to modify. If 0, the calling thread will be passed.
 * @param mask The cpu_set_t param used to set the affinity. See man cpu_set for details
 * @returns 0 - For success \\
 * @returns Otherwise - The error returned by the syscall sched_setaffinity() */
int set_sched_affinity(pid_t pid, cpu_set_t * mask) {
    int res = 0;
    if ((res = sched_setaffinity(pid, sizeof(cpu_set_t), mask)))
        perror("set_sched_affinity");
    return res;
}


/** Function used to print the affinity mask of a process or thread. 
 * @param pid The pid of the process to modify. If 0, the calling thread will be passed.
 * @returns 0 - For success \\
 * @returns Otherwise - The error returned by the syscall sched_setaffinity() */
int print_sched_affinity(pid_t pid) {
    int res = 0;
    cpu_set_t mask;
    CPU_ZERO(&mask);    // it clears the mask and let us write on it
    if ((res = sched_getaffinity(pid, sizeof(cpu_set_t), &mask)))
        perror("set_sched_affinity");
    if (!res)
        fprintf(stderr, "Affinity mask for thread %u:\t%u/%ld\n", pid, CPU_COUNT(&mask), N_PROCESSORS);
    return res;
}


/** Function used to print the content of a sched_attr structure. 
 * @param attr A pointer to the struct to print. If `*attr` is `NULL` the print will clarify it.
 * @note To retrieve and print the attributes of a thread, please use `print_thread_attributes`. */
void print_sched_attr(struct sched_attr * attr) {
    if (attr == NULL) {
        fprintf(stderr, "SCHED_ATTR: %p\n", attr);
        return;
    }
    fprintf(stderr, "SCHED_ATTR: {size: %u, policy: %u, flags: %llu, nice: %u, priority: %u, runtime: %llu, deadline: %llu, period: %llu}\n", 
            attr->size, attr->sched_policy, attr->sched_flags, attr->sched_nice, attr->sched_priority, attr->sched_runtime, attr->sched_deadline, attr->sched_period);
    return;
}


/** Function used to print on `stderr` the attributes of the calling thread. 
 * @param thread_id The TID of the calling thread. Usually stored in `ff/node.hpp`.
 * @returns 0 - Success \\
 * @returns Otherwise - The error returned by the system call.
 * @note I preferred to use the thread_id already stored in the "ff/node.hpp" to avoid issues about it.
*/
int print_thread_attributes(size_t thread_id) {
    struct sched_attr printable;
    int result = 0;
    if ((result = syscall(SYS_sched_getattr, thread_id, ((struct sched_attr *)&printable), sizeof(struct sched_attr), 0)) != 0) {
        perror("print_thread_attributes");
        return result;
    }

    if (printable.sched_policy == 0) {
        fprintf(stderr, "Thread %ld={policy: %u, flags: %llu, nice: %u, priority: %u}\n", thread_id, 
            printable.sched_policy, printable.sched_flags, printable.sched_nice, printable.sched_priority);
    } else {
        fprintf(stderr, "Thread %ld={policy: %u, flags: %llu, nice: %u, priority: %u, [RT: %llu, DL: %llu, period: %llu]}\n", 
            thread_id, printable.sched_policy, printable.sched_flags, printable.sched_nice, printable.sched_priority, 
                  printable.sched_runtime, printable.sched_deadline, printable.sched_period);
    }
    return result;
}

/** Function used to SET the attributes of the current thread, using a `sched_attr` structure. 
 * @param attr is a pointer to the structure of `sched_attr` type, which will contain all attributes required for the scheduling.
 * @param thread_id The TID of the calling thread. Usually stored in `ff/node.hpp`.
 * @returns 0 - For success \\
 * @returns Otherwise - The error returned by the system call. */
int set_scheduling_out(struct sched_attr * attr, size_t thread_id) {
    if (attr == NULL) {
        perror("NULL attr in set_scheduling_out");
        return -1;
    }
    int result = 0;
    cpu_set_t mask;
    CPU_ZERO(&mask);        // cleaning the mask
    for (int i = 0; i < N_PROCESSORS; CPU_SET(i++, &mask)); // setting all cpu in the mask
    
    if ((result = set_sched_affinity(thread_id, &mask))) {
        perror("set_scheduling_out: Error in set_sched_affinity");
    } else if ((result = syscall(SYS_sched_setattr, thread_id, attr, 0)) != 0) {
        perror("set_scheduling_out: Error in syscall");
        print_sched_affinity(thread_id);
    }
    print_sched_affinity(thread_id);
    return result;
}

#endif
