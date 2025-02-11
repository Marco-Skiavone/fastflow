#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

/** It seems that for the systems using:
 * - Ubuntu 24.04.1 
 * - Kernel 6.8.0-51-generic) 
 * The following include contains the sched_attr struct required for the scheduling. 
 * Credits to: https://github.com/torvalds/linux/blob/master/include/linux/sched.h */
#include <linux/sched/types.h>

#ifndef _SCHED_MONITORING_H
#define _SCHED_MONITORING_H 1

/** Function used to print on stdout the attributes of the calling thread. 
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

    std::cout << "Thread " << thread_id << ": {size: " << printable.size << ", policy: " << printable.sched_policy << 
            " (0 for SCHED_OTHER), flags: " << printable.sched_flags << ", nice: " << printable.sched_nice << ", priority: "
             << printable.sched_priority << "}\n";
    return result;
}

#endif
