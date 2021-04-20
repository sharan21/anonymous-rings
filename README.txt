# Note
I am running MacOS and need to use clang++ as g++11 does not support -fopenmp option. Please use the below commands only if you are using MacOS. For other systems, G++ can be used and OpenMP can be included and linked, something like this should work: 
> g++ -std=c++11 -fopenmp test.cpp -o test -lomp

Please let me know if you are not able to run the code for some reason.

# How to run

 ClockSync uses a dedicated message passing server (message-server.cpp) that needs to be run before runnning the respective algo.

For Vector Clocks, run:

> clang++ -Xpreprocessor -fopenmp message-server.cpp -o message-server -lomp
> ./message-server-vc
> clang++ -std=c++11 -Xpreprocessor -fopenmp VC-cs20mtech14003.cpp -o VC-cs20mtech14003 -lomp
> ./VC-cs20mtech14003

