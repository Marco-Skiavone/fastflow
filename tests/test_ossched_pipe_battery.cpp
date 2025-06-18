/* This file has been written by Marco Schiavone during an internship, supervised by the Professor Enrico Bini. */
/** **************************************************************
  * This file executes the "test_ossched_pipe" executable an arbitrary number of times (argv[1]),
 * passing as parameters all nodes from 1 to 6 (inclusive) and the number of requested tasks 
 * (default: 100,000).
 *
 * This code is designed to collect data on the performance of the test more efficiently.
 * The test itself autonomously updates some files in .csv format.
 */
/** @author: Marco Schiavone */

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#define MAX_STR_LEN 40

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "launch application as \"./test_ossched_pipe_battery <ntimes> <?tasks>\"" << std::endl;
        return -1;
    }
    int chld = -1, status;
    int tasks_to_do = 100000;
    int times_to_cycle = 0;
    
    if (argc > 2)   // default as 50000
        tasks_to_do = atoi(argv[2]);
    
    char **argv_child = (char**) malloc(sizeof(char *) * 3);
    argv_child[0] = (char *) malloc(sizeof(char) * MAX_STR_LEN);
    argv_child[1] = (char *) malloc(sizeof(char) * MAX_STR_LEN);
    argv_child[2] = (char *) malloc(sizeof(char) * MAX_STR_LEN/2);
    argv_child[3] = NULL;
    
    const char * test_str = "./test_ossched_pipe"; 

    if(!sprintf(argv_child[0], "%s", test_str)) {
        std::cerr << "Error in nodes sprintf()! argv_child[0]" << std::endl;
        free(argv_child[2]);
        free(argv_child[1]);
        free(argv_child);
        return -1;
    }

    if (!sprintf(argv_child[1], "%d", tasks_to_do)) {
        std::cerr << "Error in sprintf()! argv_child[1]" << std::endl;
        free(argv_child[2]);
        free(argv_child[1]);
        free(argv_child);
        return -1;
    }

    times_to_cycle =  atoi(argv[1]);
    std::cout << "Starting tests:" << std::endl;

    for (int nodes = 1; nodes < 7; ++nodes) {
        std::cout << "\n\n\n\n\n\n\n\n----- nodes: " << nodes << " -----\n\n\n\n\n\n\n\n";
        if (!sprintf(argv_child[2], "%d", nodes)) {
            std::cerr << "Error in nodes sprintf()! Nodes: " << nodes << std::endl;
            free(argv_child[2]);
            free(argv_child[1]);
            free(argv_child);
            return -1;
        }
        for (int i = 0; i < times_to_cycle; ++i) {
            switch (chld = fork()) {
                case 0:
                    execv("./test_ossched_pipe", argv_child);
                    std::cerr << "Error! Got on this line after fork! Child #" << i << std::endl;
                    return -1;
                case -1: 
                    std::cerr << "Error! Got -1 in fork! Child #" << i << std::endl;
                    free(argv_child[2]);
                    free(argv_child[1]);
                    free(argv_child);
                    return -1;
                default:
                    wait(&status);
                    if (!WIFEXITED(status)) {
                        std::cerr << "Error! Got invalid status by a child! Child #" << i << std::endl;
                        std::cerr << "status is " << status << std::endl;
                        free(argv_child[2]);
                        free(argv_child[1]);
                        free(argv_child);
                        return -1;
                    }
            }
        }
    }
    std::cout << "Test battery has finished. Exiting..." << std::endl;
    free(argv_child[2]);
    free(argv_child[1]);
    free(argv_child);
    return 0;
}
