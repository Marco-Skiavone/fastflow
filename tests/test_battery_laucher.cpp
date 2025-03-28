#include <stdio.h>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#define MAX_STR_LEN 40

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "launch application as \"./test_battery_launcher <ntimes> <?interval>\"" << std::endl;
        return -1;
    }
    int chld = -1, interval = 50000, status;
    size_t tasks = 0;

    if (argc > 2)   // default as 50000
        interval = atoi(argv[2]);
    
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
        //free(argv_child[0]);
        free(argv_child);
        return -1;
    }
    
    std::cout << "Starting tests:" << std::endl;

    //for (int nodes = 1; nodes < 7; ++nodes) {
    for (int nodes = 6; nodes < 7; ++nodes) {
        if (!sprintf(argv_child[2], "%d", nodes++)) {
            std::cerr << "Error in nodes sprintf()! Nodes: " << nodes-1 << ", Tasks: " << tasks << std::endl;
            free(argv_child[2]);
            free(argv_child[1]);
            //free(argv_child[0]);
            free(argv_child);
            return -1;
        } 
        std::cout << "\n\n\nnodes: " << atoi(argv_child[2]) << "\n\n\n" << std::endl;
        for (int i = 0; i < atoi(argv[1]); ++i) {
            if (i % 10 == 0) {
                tasks += interval;
                if (!sprintf(argv_child[1], "%ld", tasks)) {
                    std::cerr << "Error in sprintf()! Child #" << i << std::endl;
                    free(argv_child[2]);
                    free(argv_child[1]);
                    //free(argv_child[0]);
                    free(argv_child);
                    return -1;
                }
            }
            
            switch (chld = fork()) {
                case 0:
                    execv("./test_ossched_pipe", argv_child);
                    std::cerr << "Error! Got on this line after fork! Child #" << i << std::endl;
                    return -1;
                case -1: 
                    std::cerr << "Error! Got -1 in fork! Child #" << i << std::endl;
                    free(argv_child[2]);
                    free(argv_child[1]);
                    //free(argv_child[0]);
                    free(argv_child);
                    return -1;
                default:
                    wait(&status);
                    if (!WIFEXITED(status)) {
                        std::cerr << "Error! Got invalid status by a child! Child #" << i << std::endl;
                        std::cerr << "status is " << status << std::endl;
                        free(argv_child[2]);
                        free(argv_child[1]);
                        //free(argv_child[0]);
                        free(argv_child);
                        return -1;
                    }
            }
        }
        std::cout << "\n.\n";
        std::cout.flush();
    }
    std::cout << "Test battery has finished. Exiting..." << std::endl;
    free(argv_child[2]);
    free(argv_child[1]);
    //free(argv_child[0]);
    free(argv_child);
    return 0;
}
