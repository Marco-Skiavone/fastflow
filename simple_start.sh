#!/bin/bash

# exit if any command fails
set -e  

cd tests

# Build
make test_ossched_pipe

# Run the simulation
sudo ./test_ossched_pipe 1000000 4
