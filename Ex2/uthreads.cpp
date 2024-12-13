//
// Created by pnina on 13/06/2024.
//

#include "uthreads.h"
#include "Thread.h"
#include <csignal>
#include <iostream> // Add this line to include the iostream header file

#include <algorithm>
#include <csetjmp>
#include <csetjmp>    // for sigjmp_buf, sigsetjmp, siglongjmp

#include <sys/time.h> // for setitimer
#include <queue>
#include <unordered_map>


#define SYS_ERR "system error: "
#define LIB_ERR "thread library error: "
#define NON_POSITIVE_QUANTUM "The quantum_usecs should be positive\n"
#define NULL_ENTRY_POINT "The entry_point should not be null pointer\n"
#define OUT_OF_SPACE "Number of threads reach to limit\n"
#define INVALID_TID "Invalid tid"
#define INVALID_TID_TERMINATION "invalid tid";
#define INVALID_TID_BLOCK "Invalid tid block";
#define MAIN_SLEEP "the main thread can't be in sleep mode\n"
#define SIGACTION_ERR "sigaction failed\n"
#define TIMER_ERR "timer error\n"
#define BLOCK_SIGNAL_ERROR "block signal failed"
#define UNBLOCK_SIGNAL_ERROR "unblock signal failed"
#define FAILURE -1
#define MAIN_THREAD_ID 0
#define  SUCCESS 0
std::queue<Thread*> _readyThreads; // The ready threads queue
std::unordered_map<int, Thread*> all_threads; // The threads map
std::vector<Thread*> _blockedThreads; // The blocked threads list
std::unordered_map<Thread*, int> _sleepingThreads; // The sleeping threads vector
std::priority_queue<int, std::vector<int>, std::greater<int>> available_ids; // The available IDs set

int _next_available_id = 1; // The next available ID
int _quantom_counter = 0;
int _total_threads = 0;
Thread* _running_thread; // The running thread
struct itimerval timer; // The timer struct
struct sigaction sa;
sigset_t set_sig;



// inner functions
int blockSignal();                         // Block signals  #CHECK
int unblockSignal();                       // Unblock signals #CHECK
int set_timer();            // Set the timer #CHECK
void set_interval_timer(suseconds_t _quantom_usec);
void remove_from_ready(int tid);            // Remove a thread from the ready queue #CHECK
void remove_from_blocked(int tid);          // Remove a thread from the blocked list #CHECK
void switch_threads(bool terminated = false);       // Switch between threads
int get_minimal_id();                       // Get the minimal available ID #CHECK

void sleep_handler();                       // Clear the scheduler data
void timer_handler(int sig);




int uthread_init(int quantum_usecs){
    blockSignal();

    if (quantum_usecs <= 0){
        std::cerr << LIB_ERR << NON_POSITIVE_QUANTUM;
        return FAILURE;
    }
    set_interval_timer(quantum_usecs);
    // create thread
    Thread* main_thread = new Thread();
    all_threads.insert({MAIN_THREAD_ID, main_thread});
    _quantom_counter++;
    _total_threads++;
    _running_thread = main_thread;
    main_thread->incrementQuantum();
    sigemptyset(&set_sig); // initialize the signal set
    sigaddset(&set_sig, SIGVTALRM); // add the SIGVTALRM signal to the set
    set_timer();
    unblockSignal();
    return 0;
}


int uthread_spawn(thread_entry_point entry_point){

    if (entry_point == nullptr){
        std::cerr << LIB_ERR << NULL_ENTRY_POINT;
        return FAILURE;
    }
    blockSignal();

    int tid = get_minimal_id();
    if (tid == FAILURE){
        unblockSignal();
        std::cerr << LIB_ERR << NULL_ENTRY_POINT;
        return FAILURE;
    }
    if (_total_threads == MAX_THREAD_NUM){
        std::cerr << LIB_ERR << OUT_OF_SPACE;
        return FAILURE;
    }
    char* stack = new char[STACK_SIZE];
    Thread* new_thread = new Thread(tid, stack, entry_point);
    all_threads.insert({tid, new_thread});
    _readyThreads.push(all_threads[tid]);
    _total_threads++;
    unblockSignal();
    return tid; 
}


int uthread_terminate(int tid){

    blockSignal();

    if (all_threads.find(tid) == all_threads.end()){
        std::cerr << LIB_ERR << INVALID_TID_TERMINATION;
        unblockSignal();
        return FAILURE;
    }
    // if the thread is the main thread
    if (tid == MAIN_THREAD_ID){
        unblockSignal();
        for (auto &thread : all_threads){
            delete thread.second;
        }

        exit(EXIT_SUCCESS);
    }

    // if the thread is the running thread
    if (tid == uthread_get_tid() && _running_thread->getState() == RUNNING){
       available_ids.push(tid);
       switch_threads(true);
    }
    // if the thread is in the ready queue
    Thread* thread = all_threads[tid];
    if( thread->getState() == READY){
        remove_from_ready(tid);
    }
    // if the thread is in the blocked queue
    if (thread->getState() == BLOCKED){
        remove_from_blocked(tid);
    }
    // if the thread is in the sleeping queue
    if (thread->isSleep()){
        _sleepingThreads.erase(all_threads[tid]);
    }
    available_ids.push(tid); // add the id to the available ids
    all_threads.erase(tid);
    delete thread;
    _total_threads--;
    unblockSignal();
    return SUCCESS;

}


int uthread_block(int tid){
    blockSignal();

    if (all_threads.find(tid) == all_threads.end() || tid == MAIN_THREAD_ID){
        std::cerr << LIB_ERR << INVALID_TID_BLOCK;
        unblockSignal();
        return FAILURE;
    }
    Thread* thread = all_threads[tid];
    if (thread->getState() == BLOCKED){
        unblockSignal();
        return SUCCESS;
    }

    if (_running_thread->getTid() == tid && _running_thread->getState() == RUNNING){
        _running_thread->setState(BLOCKED);
        switch_threads();	
        
    }
    else if (thread->getState() == READY){
        remove_from_ready(tid);
        thread->setState(BLOCKED);
        _blockedThreads.push_back(all_threads[tid]);
    }
    unblockSignal();
    return SUCCESS;
}

int uthread_get_tid(){
    return _running_thread->getTid();

}

int uthread_resume(int tid){
    blockSignal();

    if (all_threads.find(tid) == all_threads.end()){
        std::cerr << LIB_ERR << INVALID_TID << "resume";
        unblockSignal();
        return FAILURE;
    }
    Thread* thread = all_threads[tid];
    if (thread->getState() == RUNNING || thread->getState() == READY) {
        unblockSignal();
        return SUCCESS; // No effect needed
    }

    if (thread->getState() == BLOCKED){
        remove_from_blocked(tid);
        thread->setState(READY);
        if (!thread->isSleep()){
            _readyThreads.push(all_threads[tid]);
        }
    }
    unblockSignal();
    return SUCCESS;
}

int uthread_sleep(int num_quantums){
    blockSignal();

    if (num_quantums <= 0){
        std::cerr << LIB_ERR << NON_POSITIVE_QUANTUM;
        unblockSignal();
        return FAILURE;
    }
    if (_running_thread->getTid() == MAIN_THREAD_ID){
        std::cerr << LIB_ERR << MAIN_SLEEP;
        unblockSignal();
        return FAILURE;
    }
    _sleepingThreads[_running_thread] = num_quantums;
    _running_thread->setSleep(true);
    _running_thread->setState(READY);
    switch_threads();
    unblockSignal();
    return SUCCESS;

}

int uthread_get_total_quantums(){
    return _quantom_counter;
}

int uthread_get_quantums(int tid){
    blockSignal();

    if (all_threads.find(tid) == all_threads.end()){
        std::cerr << LIB_ERR << INVALID_TID << "get quantoms\n";
        unblockSignal();
         return FAILURE;
    }
    int quantums = all_threads[tid]->getQuantum();
    unblockSignal();
    return quantums; 
}
////////////////////////////////////
// inner functions implementation //
////////////////////////////////////
int blockSignal(){
    sigset_t mask;
    sigemptyset (&mask);
    sigaddset (&mask, SIGVTALRM);
    if (sigprocmask (SIG_BLOCK, &mask, nullptr) < 0)
    {
        fprintf (stderr, BLOCK_SIGNAL_ERROR);
        return -1;
    }
    return 0;
}

int unblockSignal(){
    sigset_t mask;
    sigemptyset (&mask);
    sigaddset (&mask, SIGVTALRM);
    if (sigprocmask (SIG_UNBLOCK, &mask, nullptr) < 0)
    {
        fprintf (stderr, UNBLOCK_SIGNAL_ERROR);
        return -1;
    }
    return 0;
}


void timer_handler(int sig){
    switch_threads();
}

// return the minimal available id
int get_minimal_id(){
    if (available_ids.empty()) {
        return _next_available_id++;
    } else {
        // Get the minimum element from minHeap
        int minID = available_ids.top();
        available_ids.pop();
        return minID;
    }
}


void sleep_handler(){
    std::vector<Thread*> waking_threads;
    for (auto &thread: _sleepingThreads){
        if (thread.second == 0){
            thread.first->setSleep(false);
            waking_threads.push_back(thread.first);
        }
        thread.second--;
    }
    for (auto &thread: waking_threads){
        _sleepingThreads.erase(thread);
        if(thread->getState() == READY) {
            _readyThreads.push(thread);
        }
    }
}

void switch_threads(bool terminated){
    blockSignal();
    sleep_handler();
    if (!terminated) {
        if (sigsetjmp(*_running_thread->getEnv(), 1) != 0) {
            unblockSignal();
            return;
        }
    }else{
        all_threads.erase(uthread_get_tid());
        delete _running_thread;
        _total_threads--;
    }
    if (!terminated && _running_thread->getState() == RUNNING) {
        _running_thread->setState(READY);
        _readyThreads.push(_running_thread);
    }
    if (!_readyThreads.empty()){
        Thread* next_thread = _readyThreads.front();
        _readyThreads.pop();
        next_thread->setState(RUNNING);
        _running_thread = next_thread;
        _running_thread->incrementQuantum();
        _quantom_counter++;

        siglongjmp(*_running_thread->getEnv(), 1);
    }
    unblockSignal();

} 

void remove_from_ready(int tid){
    std::queue<Thread*> temp;
    while (!_readyThreads.empty()){
        Thread* thread = _readyThreads.front();
        _readyThreads.pop();
        if (thread->getTid() != tid){
            temp.push(thread);
        }
    }
    _readyThreads = temp;
}

void remove_from_blocked(int tid) {
    _blockedThreads.erase(std::remove_if(_blockedThreads.begin(), _blockedThreads.end(),
                    [tid](Thread* t) { return t->getTid() == tid; }), _blockedThreads.end());
}

int set_timer(){
    // set the timer

    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, nullptr)< 0){
        unblockSignal();
        std::cerr << SYS_ERR << SIGACTION_ERR;
        exit(EXIT_FAILURE);
    }

    // start the virtual timer that counts down whenever this process is executing
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)){
        unblockSignal();
        std::cerr << SYS_ERR << TIMER_ERR;
        exit(EXIT_FAILURE);
    }

    unblockSignal();
    return SUCCESS;
}

void set_interval_timer(suseconds_t _quantom_usec){

    timer.it_value.tv_sec = _quantom_usec / 1000000;
    timer.it_value.tv_usec = _quantom_usec % 1000000;

    timer.it_interval.tv_sec = _quantom_usec / 1000000;
    timer.it_interval.tv_usec = _quantom_usec % 1000000;

}
