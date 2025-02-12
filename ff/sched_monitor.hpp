#ifndef _SCHED_MONITORING_H
#define _SCHED_MONITORING_H 1

/** It seems that for the systems using:
 * - Ubuntu 24.04.1 
 * - Kernel 6.8.0-51-generic) 
 * The following include contains the sched_attr struct required for the scheduling. 
 * Credits to: https://github.com/torvalds/linux/blob/master/include/linux/sched.h */
#include <linux/sched/types.h>

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

/** Function used to print on `stderr` the attributes of the calling thread. 
 * @param thread_id The TID of the calling thread. Usually stored in ff/node.hpp
 * @returns 0 - Success\\
 * @returns Otherwise - The error returned by the system call.
 * @note I preferred to use the thread_id already stored in the "ff/node.hpp" to avoid issues about it.
*/
int print_thread_attributes(size_t thread_id) {
    struct sched_attr printable;
    int result = 0;
    if ((result = syscall(SYS_sched_getattr, 0, ((struct sched_attr *)&printable), sizeof(struct sched_attr), 0)) != 0) {
        perror("print_thread_attributes");
        return result;
    }

    fprintf(stderr, "Thread %ld: {size: %u, policy: %u (0 for SCHED_OTHER), flags: %llu, nice: %u, priority: %u}\n", thread_id, 
        printable.size, printable.sched_policy, printable.sched_flags, printable.sched_nice, printable.sched_priority);
    return result;
}


/** Function used to SET the attributes of the current thread, using a `sched_attr` structure. 
 * @param attr is a pointer to the structure of `sched_attr` type, which will contain all attributes required for the scheduling.
 * @returns 0 - Success\\
 * @returns Otherwise - The error returned by the system call.
 */
int set_scheduling_out(struct sched_attr * attr) {
    int result = 0;
    if ((result = syscall(SYS_sched_setattr, 0, (struct sched_attr *)attr, sizeof(struct sched_attr), 0)) != 0)
        perror("set_scheduling_out");
    
    return result;
}

#endif
