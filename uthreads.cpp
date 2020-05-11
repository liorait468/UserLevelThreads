
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include "uthreads.h"
#include "thread.h"
#include <list>
#include <map>
#include <iostream>

#define MICRO_TO_SEC 1000000
#define ERROR_IND -1
#define SUCCESS_IND 0
#define MAIN_PRIORITY 0
#define RETURN_VALUE 5
#define INVALID_INPUT_ERR "thread library error: .Invalid input"
#define TOO_MANY_THREADS_ERR "thread library error: .too many threads"
#define SYSTEM_CALL_STR "system error: "
#define ALLOC_FAILED "allocation failed!"
#define SIGACTION_ERROR "sigaction error."
#define SIGPROCMASK_ERROR "sigprocmask error."
#define SETITTIMER_ERROR "setitimer error."

static int current_running_thread = 0; // saves the id of the current running thread, private
static sigjmp_buf env[MAX_THREAD_NUM]; // buffer array in size MAX_THREAD_NUM, private
static int* quantums_array; // the size is given in init function, private

/* the size of the quantums array */
int sizeOfQuantumsArray = 0;

/* saves the current number of threads in the system */
int currentNumOfThreads = 0;

/* holds the total number of quantums that where started since the library was initializes */
int totalNumOfQuantums = 0;

/* timer structs */
struct sigaction sa = {};
struct itimerval timer;

/* the list of threads in READY state */
std::list<Thread*> ready_list;

/* the list of threads in BLOCK state */
std::list<Thread*> block_list;

/* a map that saves all the threads in the system */
std::map<int, Thread*> threadsMap;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif

// private function that finds and returns the next empty id in the map of threads
int nextIdEmpty()
{
    int nextId = 0;
    for(auto it = threadsMap.begin(); it != threadsMap.end(); it++)
    {
        if ((*it).second->getId() != nextId)
        {
            break;
        }
        nextId++;
    }
    return nextId;
}

/**
 * This function release the allocated memory in the program.
 */
void releaseMemory()
{
    ready_list.clear();
    block_list.clear();
    for (auto & it : threadsMap)
    {
        delete it.second;
    }
    threadsMap.clear();

    // delete the array of quantums
    delete [] quantums_array;
}

/**
 * @param threadId - the id of the thread
 * @return true if the thread with the given id is in the map, and false otherwise.
 */
bool isInMap(int threadId)
{
    auto it = threadsMap.find(threadId);

    // If the thread doesn't exist, returns false
    return !(it == threadsMap.end());
}

/**
 * block signals in order to avoid context switch in the middle of an operation.
 */
int blockSignals(sigset_t *set)
{
    sigemptyset(set);
    sigaddset(set, SIGVTALRM);

    // Checks if sigprocmask worked
    if (sigprocmask(SIG_BLOCK, set, NULL) == ERROR_IND)
    {
        std::cerr << SYSTEM_CALL_STR << SIGPROCMASK_ERROR << std::endl;
        return ERROR_IND;
    }
    return SUCCESS_IND;
}

// an empty function for creating the main thread
void voidFunction(void){}

// This function is called every time the timer expires
// the function moves the current thread to the end of the list
// the function runs the next thread that is in READY state
void timer_handler(int sig)
{
    (void)sig;

    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    // If the ready list isn't empty, the next thread will run
    if (!ready_list.empty())
    {
        Thread* newThread = ready_list.front(); // Gets the new thread in READY state
        ready_list.pop_front(); // removes the next thread from the list

        if (isInMap(current_running_thread)) // the thread not terminated and exists in the map
        {
            Thread* oldThread = threadsMap[current_running_thread];
            if (oldThread->getState() != BLOCK)
            {
                oldThread->setState(READY);
                ready_list.push_back(oldThread); //push back current thread
            }
        }
        int oldThreadId = current_running_thread;
        current_running_thread = newThread->getId(); // change the current running thread
        newThread->setState(RUNNING);

        // set timer for new thread
        timer.it_value.tv_sec     = 1 + quantums_array[newThread->getId()] / MICRO_TO_SEC;
        timer.it_value.tv_usec    = quantums_array[newThread->getId()] % MICRO_TO_SEC;
        timer.it_interval.tv_sec  = quantums_array[newThread->getId()] / MICRO_TO_SEC;
        timer.it_interval.tv_usec = quantums_array[newThread->getId()] % MICRO_TO_SEC;
        
        // Start a virtual timer. It counts down whenever this process is executing.
        if (setitimer (ITIMER_VIRTUAL, &timer, NULL) == ERROR_IND) {
            std::cerr << SYSTEM_CALL_STR << SETITTIMER_ERROR << errno << std::endl;
            exit (1);
        }

        threadsMap[current_running_thread]->increaseNumOfQuantums();
        totalNumOfQuantums++; // Increases the total number of quantums

        int ret = sigsetjmp(env[oldThreadId], 1);
        // Checks if ret equals the value returned from siglongjmp
        if (ret == RETURN_VALUE)
        {
            return;
        }
        siglongjmp(env[current_running_thread], RETURN_VALUE); // jump to the next thread
    }

    // Checks if sigprocmask worked
    if (sigprocmask(SIG_UNBLOCK, &set, NULL) == ERROR_IND)
    {
        std::cerr << SYSTEM_CALL_STR << SIGPROCMASK_ERROR << std::endl;
        exit (1);
    }
}

/**
 * This method checks if the given quantums array has valid values (positive).
 * @param quantums an array of the length of a quantum in micro-seconds for each priority.
 * @param size size the size of the array.
 * @return true if all the quantums in the array are valid (positive) values, false otherwise.
 */
bool checkQuantumsArray(int* quantums, int size)
{
    // Goes over the array of quantums to check valid values
    for (int i = 0; i < size; i++)
    {
        // Checks if the value is negative, if yes, returns an error
        if (quantums[i] <= 0)
        {
            return false;
        }
        quantums_array[i] = quantums[i]; // save the quantum for each priority id
    }
    return true;
}

/**
 * This function initializes the thread library.
 * It is an error to call the function with quantum_usecs array containing non-positive integer.
 * @param quantum_usecs an array of the length of a quantum in micro-seconds for each priority.
 * @param size the size of the array.
 * @return On success, return 0. On failure, return -1.
 */
int uthread_init(int *quantum_usecs, int size)
{
    // Checks if the array is null
    if (quantum_usecs == nullptr)
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    // Checks if size is smaller then zero
    if (size <= 0)
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    quantums_array = new int[size];

    // Checks if the memory allocation worked
    if (quantums_array == nullptr)
    {
        std::cerr << SYSTEM_CALL_STR << ALLOC_FAILED << std::endl;
        exit(1);
    }
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    sizeOfQuantumsArray = size;

    // Checks if the quantums array contains valid values
    if (!checkQuantumsArray(quantum_usecs, size))
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    // creates the main thread and saves it in the array of threads
    uthread_spawn(&voidFunction, MAIN_PRIORITY);

    // set the main thread as RUNNING thread
    threadsMap[0]->setState(RUNNING);
    totalNumOfQuantums++; // adds the quantum of main

    // increase the number of quantums of the main thread
    threadsMap[current_running_thread]->increaseNumOfQuantums();

    // set timer

    // install timer_handler as the signal handler for SIGVTALRM
    sa.sa_handler = &timer_handler;

    // Checks if the operation worked
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        std::cerr << SYSTEM_CALL_STR << SIGACTION_ERROR << std::endl;
        exit(1);
    }

    timer.it_value.tv_sec     = 1;
    timer.it_value.tv_usec    = 0;
    timer.it_interval.tv_sec  = quantums_array[0] / MICRO_TO_SEC;
    timer.it_interval.tv_usec = quantums_array[0] % MICRO_TO_SEC;

    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer (ITIMER_VIRTUAL, &timer, NULL) == ERROR_IND)
    {
        std::cerr << SYSTEM_CALL_STR << SETITTIMER_ERROR << std::endl;
        exit(1);
    }
    return SUCCESS_IND;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * priority - The priority of the new thread.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void), int priority)
{
    // Checks if the function is null
    if (f == nullptr)
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    // Checks if priority argument is valid
    if ((priority < 0) || (priority >= MAX_THREAD_NUM) || (priority >= sizeOfQuantumsArray))
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    // Checks if we reached the maximum number of threads
    if (currentNumOfThreads >= MAX_THREAD_NUM)
    {
        std::cerr << TOO_MANY_THREADS_ERR << std::endl;
        return ERROR_IND;
    }

    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    int newThreadId = 0;
    if (nextIdEmpty() == 0)
    {
        newThreadId = 0;
    }
    else
    {
        newThreadId = nextIdEmpty();
    }

    // creating a new thread

    Thread *newThread = new Thread(newThreadId, priority, quantums_array[priority],
                            READY, f);

    sigprocmask(SIG_UNBLOCK, &set, NULL);

    address_t sp, pc;
    sp = (address_t)newThread->stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t)f;
    sigsetjmp(env[newThreadId], 1); // save the current state to the first thread
    (env[newThreadId]->__jmpbuf)[JB_SP] = translate_address(sp); // save into env the sp
    (env[newThreadId]->__jmpbuf)[JB_PC] = translate_address(pc); // save into env the pc
    sigemptyset(&env[newThreadId]->__saved_mask);

    // adding the new thread to the list of threads (if the thread isn't the main thread)
    if (newThreadId != 0)
    {
        ready_list.push_back(newThread);
    }
    threadsMap[newThreadId] = newThread;
    currentNumOfThreads++; // Increases the number of threads in the system
    return newThreadId;
}


/*
 * Description: This function changes the priority of the thread with ID tid.
 * If this is the current running thread, the effect should take place only the
 * next time the thread gets scheduled.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_change_priority(int tid, int priority)
{
    // Checks if the priority parameter is valid
    if ((priority < 0) || (priority >= MAX_THREAD_NUM) || (priority >= sizeOfQuantumsArray))
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    if (!isInMap(tid) || tid == 0)
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }
    threadsMap[tid]->setPriority(priority);
    return SUCCESS_IND;
}


/**
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * @param tid the id of the thread we want to terminate
 * @return 0 on success -1 on fail. If a thread terminates itself or the main thread is terminated,
 * the function does not return.
 */
int uthread_terminate(int tid)
{
    // If the tid doesn't exist, prints error message and returns
    if (!isInMap(tid))
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    // If the thread to terminate is the main thread, exits the program
    if (tid == 0)
    {
        // releases all memory in the system
        releaseMemory();
        exit(0);
    }

    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    // releases the thread's memory
    if (threadsMap[tid]->getState() == READY)
    {
        ready_list.remove(threadsMap[tid]);
    }
    delete threadsMap.find(tid)->second;
    threadsMap.erase(tid);
    currentNumOfThreads--;

    sigprocmask(SIG_UNBLOCK, &set, NULL);
    // If the thread to terminate is the current thread, moves to another thread
    if (tid == current_running_thread)
    {
        timer_handler(1);
    }
    return SUCCESS_IND;
}

/**
 * This function blocks the thread with ID tid. If no thread with ID tid exists it is considered
 * an error. In addition, it is an error to try blocking the main thread (tid == 0).
 * If a thread blocks itself, a scheduling decision should be made. Blocking a thread in BLOCKED
 * state has no effect and is not considered an error.
 * @param tid - the id of the current thread
 * @return On success, return 0. On failure, return -1.
 */
int uthread_block(int tid)
{
    // Checks if the thread is in the map
    if (!isInMap(tid) || tid == 0)
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        return ERROR_IND;
    }

    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    // If the thread to block is in the ready list, removes it from the list
    if (threadsMap[tid]->getState() == READY)
    {
        //remove the blocked thread from the ready list
        ready_list.remove(threadsMap[tid]);
    }

    threadsMap[tid]->setState(BLOCK);
    block_list.push_back(threadsMap[tid]);

    sigprocmask(SIG_UNBLOCK, &set, NULL);

    // The current running thread blocks itself
    if (tid == current_running_thread)
    {
        timer_handler(1);
    }
    return SUCCESS_IND;
}

/**
 * This function resumes a blocked thread with the given id and moves it to the READY state.
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered an error.
 * If no thread with ID tid exists it is considered an error.
 * @param tid - the id of the current thread
 * @return On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid)
{
    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    // Checks if the threads is in the map
    if (!isInMap(tid))
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return ERROR_IND;
    }

    // If the state of the thread is READY or RUNNING no need to resume the thread
    if ((threadsMap[tid]->getState() == READY) || (threadsMap[tid]->getState() == RUNNING))
    {
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return SUCCESS_IND;
    }

    block_list.remove(threadsMap[tid]);
    threadsMap[tid]->setState(READY);
    ready_list.push_back(threadsMap[tid]);

    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return SUCCESS_IND;
}

/**
 * This function returns the thread ID of the calling thread.
 * @return The ID of the calling thread.
 */
int uthread_get_tid()
{
    return threadsMap[current_running_thread]->getId();
}

/**
 * Description: This function returns the total number of quantums that were started since the
 * library was initialized, including the current quantum. Right after the call to uthread_init,
 * the value should be 1. Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * @return The total number of quantums.
 */
int uthread_get_total_quantums()
{
    return totalNumOfQuantums;
}

/**
 * Description: This function returns the number of quantums the thread with ID tid was in RUNNING
 * state. On the first time a thread runs, the function should return 1. Every additional quantum
 * that the thread starts should increase this value by 1 (so if the thread with ID tid is in
 * RUNNING state when this function is called, include also the current quantum). If no thread with
 * ID tid exists, it is considered an error.
 * @param tid - the id of the current thread
 * @return On success, return the number of quantums of the thread with tid. On failure, return -1.
 */
int uthread_get_quantums(int tid)
{
    sigset_t set;
    if(blockSignals(&set) == ERROR_IND)
    {
        exit(1);
    }

    // Checks if the thread is in the map
    if (!isInMap(tid))
    {
        std::cerr << INVALID_INPUT_ERR << std::endl;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return ERROR_IND;
    }
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return threadsMap[tid]->getNumOfQuantums();
}

