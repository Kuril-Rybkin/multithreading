#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
class COptimizer {
public:
    static bool usingProgtestSolver(void) {
        return true;
    }

    static void checkAlgorithm(AProblem problem) {
        problem = nullptr;
        // dummy implementation if usingProgtestSolver() returns true
    }

    void start(int threadCount) {
        if (receivers == 0) {
            return;
        }
        workers = threadCount;

        for (auto i : companies) {
            recv_threads.emplace_back(make_shared<thread>(&COptimizer::recv_thread, this, i));
            send_threads.emplace_back(make_shared<thread>(&COptimizer::send_thread, this, i));
        }

        for (int i = 0; i < threadCount; i++) {
            worker_threads.emplace_back(make_shared<thread>(&COptimizer::worker_thread, this));
        }
    }

    void stop(void) {
        for (auto i : recv_threads) {
            i->join();
        }

        for (auto i : send_threads) {
            i->join();
        }

        for (auto i : worker_threads) {
            i->join();
        }
    }

    void addCompany(ACompany company) {
        receivers++;
        auto tmp = make_shared<TCompany>(company);
        companies.push_back(tmp);
    }

private:
    template<typename T>
    class CThread_queue {
    public:
        bool empty() {
            unique_lock<mutex> ul(queue_mtx);
            return t_queue.empty();
        }

        void enqueue(T item) {
            unique_lock<mutex> ul(queue_mtx);
            t_queue.push(item);
            is_empty.notify_one();
        }

        T dequeue() {
            unique_lock<mutex> ul(queue_mtx);
            is_empty.wait(ul, [&]() {
                return !t_queue.empty();
            });

            auto ret = t_queue.front();
            t_queue.pop();

            return ret;
        }

    private:
        mutex queue_mtx;
        condition_variable is_empty;
        queue<T> t_queue;
    };

    /// Structure for solving a pointer to a problem pack and a counter of remaining unsolved problems in the pack.
    struct TProblem {
        TProblem(AProblemPack pack) {
            m_pack = pack;
            m_problems = pack->m_Problems.size();
        }

        AProblemPack m_pack;
        int m_problems;
        mutex m_mtx;
    };

    /// Structure containing the company information and the queue of problem packs received from the company.
    struct TCompany {
        // Constructor which takes shared_ptrs to receive and send communication threads, and pointer to company
        TCompany(ACompany company) : m_company(company) {}

        const ACompany m_company;

        CThread_queue<shared_ptr<TProblem>> m_problem_queue;
    };

    vector<shared_ptr<TCompany>> companies;
    vector<shared_ptr<thread>> recv_threads;
    vector<shared_ptr<thread>> send_threads;
    vector<shared_ptr<thread>> worker_threads;

    int workers = 0;

    mutex mtx_thread_ctr;
    int receivers = 0;

    /// Queue of unsolved solvers, paired with a vector of Problem packs. If a solver has N problems from a problem pack,
    /// then the TProblem struct pointer is pushed back to the vector N times
    CThread_queue<pair<AProgtestSolver , vector<shared_ptr<TProblem>>>> unsolved;

    /// Current progtest solver being filled up with problems by recv_threads
    mutex mtx_current;
    AProgtestSolver current_solver = createProgtestSolver();
    vector<shared_ptr<TProblem>> current_packs;

    mutex mtx_senders;
    condition_variable senders;

    void recv_thread(shared_ptr<TCompany> company) {
        AProblemPack new_pack = company->m_company->waitForPack();

        /// While not receiving a nullptr
        while (new_pack) {
            /// Create a new structure associated with the Problem pack and get a shared pointer to it.
            auto pack_struct = make_shared<TProblem>(new_pack);

            /// Insert the problem pack to the queue of problems from the company
            company->m_problem_queue.enqueue(pack_struct);

            /// Process every problem in the problem pack
            unique_lock<mutex> ul_current(mtx_current);
            for (auto i : new_pack->m_Problems) {
                /// Insert problem into solver
                current_solver->addProblem(i);
                /// Insert pack structure corresponding to the problem pushed to solver
                current_packs.push_back(pack_struct);

                if (!current_solver->hasFreeCapacity()) {
                    unsolved.enqueue(make_pair(current_solver, current_packs));

                    current_solver = createProgtestSolver();
                    current_packs.clear();
                }
            }
            ul_current.unlock();

            new_pack = company->m_company->waitForPack();
        }

        /// Receiver finished listening. Decrement the number of listening receivers.
        mtx_thread_ctr.lock();
        receivers--;

        if (receivers == 0) {
            unique_lock<mutex> ul_current(mtx_current);
            /// Make sure that the last non-pushed non-empty solver is also pushed to the queue of solvers
            unsolved.enqueue(make_pair(current_solver , current_packs));

            /// Add nullptrs to the solver queue for every workers so they know when to stop
            for (int i = 0; i < workers; i++) {
                unsolved.enqueue(make_pair(nullptr, vector<shared_ptr<TProblem>>()));
            }
        }

        mtx_thread_ctr.unlock();

        /// Push a nullptr to the problem pack queue so the sender threads know when to stop.
        company->m_problem_queue.enqueue(nullptr);
    }

    void worker_thread() {
        while (true) {
            auto solver = unsolved.dequeue();

            /// nullptr is a stop signal to terminate the thread
            if (solver.first == nullptr) {
                return;
            }

            /// Solve the solver after it was dequeued
            solver.first->solve();

            /// Since for every problem in the solver, there is a corresponding problem to it's originating problem pack
            for (auto i : solver.second) {
                unique_lock<mutex> ul_senders(mtx_senders);
//                printf("TRYING TO TAKE OWNERSHIP OF MUTEX IN WORKER...\n");
//                unique_lock<mutex> ul_tproblem(i->m_mtx);
//                printf("...WORKER GOT MUTEX\n");
                /// Decrement the number of remaining unsolved problems (A problem was just solved by the solver)
                i->m_problems--;

                /// If no remaining unsolved problems left, notify all the senders.
                /// The senders will then themselves check if the next problem in queue is solved or not using the
                /// predicate in the wait() function
                if (i->m_problems == 0) {
                    senders.notify_all();
                }
            }
        }
    }

    void send_thread(shared_ptr<TCompany> company) {
        while (true) {
            auto pack = company->m_problem_queue.dequeue();

            /// Nullptr is signal to stop
            if (pack == nullptr) {
                return;
            }

            /// Wait until a signal is received to continue and check that the pack infront is solved before sending it
            unique_lock<mutex> ul_senders(mtx_senders);
            senders.wait(ul_senders, [&]() {
//                printf("TRYING TO TAKE OWNERSHIP OF MUTEX IN SENDER...\n");
//                unique_lock<mutex> ul(pack->m_mtx);
//                printf("...SENDER GOT MUTEX\n");
                if (pack->m_problems == 0) {
                    return true;
                }
                else {
                    return false;
                }
            });
            ul_senders.unlock();

            company->m_company->solvedPack(pack->m_pack);
        }
    }
};

#ifndef __PROGTEST__

int main(void) {
    for (int i = 0; i < 1000000; i++) {
        cout << "============================================ RUN NO." << i << "========================================" << endl;
        COptimizer optimizer;
        ACompanyTest company = make_shared<CCompanyTest>();
        ACompanyTest company2 = make_shared<CCompanyTest>();
        ACompanyTest company3 = make_shared<CCompanyTest>();
        ACompanyTest company4 = make_shared<CCompanyTest>();
        ACompanyTest company5 = make_shared<CCompanyTest>();
        ACompanyTest company6 = make_shared<CCompanyTest>();

        optimizer.addCompany(company);
        optimizer.addCompany(company2);
        optimizer.addCompany(company3);
        optimizer.addCompany(company4);
        optimizer.addCompany(company5);
        optimizer.addCompany(company6);
        optimizer.start(5);
        optimizer.stop();
        if (!company->allProcessed())
            throw logic_error("(some) problems were not correctly processsed");
        if (!company2->allProcessed())
            throw logic_error("(some) problems were not correctly processsed");
        if (!company3->allProcessed())
            throw logic_error("(some) problems were not correctly processsed");
        if (!company4->allProcessed())
            throw logic_error("(some) problems were not correctly processsed");
        if (!company5->allProcessed())
            throw logic_error("(some) problems were not correctly processsed");
        if (!company6->allProcessed())
            throw logic_error("(some) problems were not correctly processsed");
    }
    return 0;
}

#endif /* __PROGTEST__ */