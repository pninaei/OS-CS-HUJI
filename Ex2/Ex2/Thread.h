//
// Created by pnina on 12/06/2024.
//

#ifndef EX2_THREAD_H
#define EX2_THREAD_H
#include "uthreads.h"
#include <setjmp.h> // Include the header file that defines sigjmp_buf
enum State {READY, RUNNING, BLOCKED};

typedef unsigned long address_t;


// clqss Thread - represents a thread in the system
class Thread {

    private:
        int _tid; // The thread's ID
        int _quantum; // The thread's quantum
        sigjmp_buf _env; // The thread's environment
        char* _stack; // The thread's stack
        bool _isSleep;
        State _state;
        thread_entry_point _entryPoint; // The thread's entry point
        void setting_env();
    public:
        Thread(int id, char* stack ,thread_entry_point entryPoint);
        Thread(); // Default constructor
        ~Thread();
        
        // setters
        void setState(State new_state);
        void setSleep(bool isSleep);

        // getters
        int getTid() const { return _tid; }
        int getQuantum() const { return _quantum; };
        State getState() const { return _state; };
        bool isSleep() const { return _isSleep;};
        sigjmp_buf* getEnv() { return &_env; };

        // Methods
        void incrementQuantum() { _quantum++; };
    
}

#endif //EX2_THREAD_H
;