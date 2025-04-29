/* This header file has been written by Marco Schiavone during an internship, supervised by the Professor Enrico Bini. */
/** **************************************************************
 * The current header file offers a variety of functions to get and set a scheduling policy on threads.
 * This file is currently included by the ff/node.hpp and can be used for testing.
 * 
 * @note For all the functions wrapping system calls, the 0 is the default return value (success).
 */
/** @author: Marco Schiavone */
#ifndef _SCHED_MONITORING_H
#define _SCHED_MONITORING_H 1

/** It seems that for the systems using:
 * - Ubuntu 24.04.1 
 * - Kernel 6.8.0-51-generic) 
 * The following include contains the sched_attr struct required for the scheduling. 
 * Credits to: https://github.com/torvalds/linux/blob/master/include/linux/sched.h 
 * UPDATE: It seems that having older version */
#include <linux/sched/types.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>

// Used below to set a mask for cpu affinity
#define N_PROCESSORS (sysconf(_SC_NPROCESSORS_ONLN))


/** Wrapper function to set a mask of cpu(s) affinity for a process.
 * @param pid The pid (or TID) of the process to modify. If 0, the calling thread will be passed.
 * @param mask The cpu_set_t param used to set the affinity. See man cpu_set for details
 * @returns 0 - For success \\
 * @returns Otherwise - The error returned by the syscall `sched_setaffinity()` */
int set_sched_affinity(pid_t pid, cpu_set_t * mask) {
    int res = 0;
    if ((res = sched_setaffinity(pid, sizeof(cpu_set_t), mask)))
        perror("set_sched_affinity");
    return res;
}


/** Wrapper function to get the scheduling attributes of a thread.
 * @param tid the tid of the Thread to check
 * @param ret the sched_attr structure reference into which to return the Thread attributes.
 * @returns 0 - For success \\
 * @returns Otherwise - The error returned by the syscall `SYS_sched_getattr` */
int get_sched_attributes(pid_t tid, struct sched_attr * ret) {
    int res = 0;
    if ((res = syscall(SYS_sched_getattr, tid, ((struct sched_attr *)ret), sizeof(struct sched_attr), 0))) {
        perror("get_sched_attributes");
    }
    return res;
}


/** Function used to print the attributes of a thread. 
 * @param thread_id The TID of the calling thread. Usually stored in `ff/node.hpp`, it can be found with the `ff/node.getOSThreadID()` function.
 * @returns 0 - Success \\
 * @returns Otherwise - The error returned by the system call.
 * @note I preferred to use the thread_id already stored in the "ff/node.hpp" to avoid issues about it. */
int print_thread_attributes(FILE * file_ptr, size_t thread_id) {
    struct sched_attr printable;
    int result = 0;
    if ((result = syscall(SYS_sched_getattr, thread_id, ((struct sched_attr *)&printable), sizeof(struct sched_attr), 0)) != 0) {
        perror("print_thread_attributes");
        return result;
    }

    if (printable.sched_policy == 0) {  // Default scheduling policy
        std::fprintf(file_ptr, "Thread %ld={policy: %u, flags: %llu, nice: %u, priority: %u}\n", thread_id, printable.sched_policy, 
            printable.sched_flags, printable.sched_nice, printable.sched_priority);
    } else {
        std::fprintf(file_ptr, "Thread %ld={policy: %u, flags: %llu, nice: %u, priority: %u, [RT: %llu, DL: %llu, period: %llu]}\n", thread_id,
            printable.sched_policy, printable.sched_flags, printable.sched_nice, printable.sched_priority, printable.sched_runtime, printable.sched_deadline, printable.sched_period);
    }
    return result;
}


/** Function used to SET the attributes of the current thread, using a `sched_attr` structure. 
 * @param attr is a pointer to the structure of `sched_attr` type, which will contain all attributes required for the scheduling.
 * @param thread_id The TID of the calling thread. Usually stored in `ff/node.hpp`.
 * @param set_affinity true to set the affinity (first call), Otherwise false (unnecessary syscall).
 * @returns 0 - For success \\
 * @returns Otherwise - The error returned by the system call. */
int set_scheduling_out(struct sched_attr * attr, size_t thread_id, bool set_affinity) {
    if (attr == NULL) {
        perror("NULL attr in set_scheduling_out");
        return -1;
    }
    int result = 0;
    /** NOTE: To be sure a deadline scheduling policy is correctly set, 
     * you should first set the affinity for all the cpus of the machine. */
    if (set_affinity) {
        cpu_set_t mask;
        CPU_ZERO(&mask);                                        // cleaning the mask
        for (int i = 0; i < N_PROCESSORS; CPU_SET(i++, &mask)); // setting all cpu in the mask
        if ((result = set_sched_affinity(thread_id, &mask))) {
            perror("set_scheduling_out: Error in set_sched_affinity");
        } 
    }
    if (!result && (result = syscall(SYS_sched_setattr, thread_id, attr, 0)) != 0) {
        perror("set_scheduling_out: Error in syscall");
    }
    return result;
}


/** Used to wrap the setting of the sched_attr in a function, used by all elements of this test. 
 * This function can make use of the functions set_scheduling_out(), set_sched_affinity().
 * 
 * @note For the first settage of the deadline policy, you should call this function with runtime=0.
 * 
 * @param n_threads the TOTAL # of threads of the simulation. Used to define a simple runtime default value of (period_deadline/n_threads)
 * @param period_deadline value to set as `period` and `deadline` of the sched attr
 * @param runtime the runtime value to set. If 0, it will be set as `period_deadline/n_threads` 
 * @param thread_id the thread id to which set the attr struct. */
 void set_deadline_attr(size_t n_threads, size_t period_deadline, size_t runtime, size_t thread_id) {
    struct sched_attr attr = {0};
    attr.size = sizeof(struct sched_attr);
    attr.sched_flags = 0;
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_deadline = period_deadline;
    attr.sched_period   = period_deadline;
    attr.sched_runtime = (runtime != 0 ? runtime : (unsigned long)(period_deadline / n_threads));
    
    // For the first settage of the deadline policy, you should call this function with runtime=0.
    if (set_scheduling_out(&attr, thread_id, runtime == 0))
        std::cerr << "Error: " << strerror(errno) << "(" << strerrorname_np(errno) << ") - (line " <<  __LINE__ << ")" << std::endl;
}


/** Used to see difference between initial and final times of the simulation.
 * @param time1 the end of time period 
 * @param time0 the start of time period
 * @returns A double representing the difference in secs. */
double diff_timespec(const struct timespec & time1, const struct timespec & time0) {
  return (time1.tv_sec - time0.tv_sec)
      + (time1.tv_nsec - time0.tv_nsec) / 1e9;
}

#endif
