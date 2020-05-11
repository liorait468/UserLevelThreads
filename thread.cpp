
#include "thread.h"
/**
 * The constructor of the thread class which includes the next values:
 * @param id
 * @param priority
 * @param quantom
 * @param state
 */
Thread::Thread(int id, int priority, int quantum, State state, void (*f)(void)) : _id(id), _priority
        (priority), _quantum(quantum), _state(state), _f(f), _numOfQuantums(0)
{
}

int Thread::getNumOfQuantums() const
{
    return _numOfQuantums;
}

/**
 * increase the number of quantoms
 */
void Thread::increaseNumOfQuantums()
{
    _numOfQuantums++;
}

/**
 * The destructor of the thread
 */
Thread::~Thread() = default;

/**
 * @return the id of the current thread
 */
int Thread::getId() const { return _id;}

/**
 * @return the priority of the current thread
 */
int Thread::getPriority() const { return _priority;}

/**
 * @return the quantom of the current thread
 */
int Thread::getQuantum() const { return _quantum;}

/**
 * @return the state of the current thread
 */
State Thread::getState() const { return _state;}

/**
 * sets the state of the current thread
 * @param state new state to set in the current thread
 */
void Thread::setState(State state) {_state = state;}

/**
 * sets the priority of the current thread
 * @param priority - new priority to set.
 */
void Thread::setPriority(int priority) {_priority = priority;}


/**
 * operator == of the Thread class. if the two Threads has the same id it returns true, else it retuns false
 */
bool Thread::operator==(const Thread & other)
{
    return _id == other.getId();
}
