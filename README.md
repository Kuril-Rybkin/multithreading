# multithreading

The goal of this task is to implement a multithreaded consumer-producer solution in C++. The task was as follows:

Create a class `COptimizer` to solve problem packs sent out by companies. For every company, there is 1 receiver and 1 sender thread. There is also an arbitrary number of worker threads, specified as an argument in the method `start(int threadCount)`.

The worker threads are intended to call computationally-intensive functions to solve the problem packs, hence the need to use multithreading. The solver was provided as a static library, and is specifically designed to be unpredictable. Each instance created with `createProgtestSolver()` has a random capacity of problems it can solve. Moreover, if a new solver is created while the number of solvers is exhausted, then it will throw random errors, produce incorrect results, or cause null pointer exceptions. It is guaranteed that the total sum of all solver capacities is enough to solve all the problem packs.

The program is also not allowed to use active waiting - CPU usage is monitored.
