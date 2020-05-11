
#ifndef EX2_THREAD_HPP
#define EX2_THREAD_HPP

#define STACK_SIZE 4096
/**
 * the possible states of a thread
 */
enum State {READY, RUNNING, BLOCK};

/**
 * A class which represents a thread
 */
class Thread
{
public:
    /**
     * The constructor of the thread class which includes the next values:
     * @param id
     * @param priority
     * @param quantom
     * @param state
     */
    Thread(int id, int priority, int quantom, State state, void (*f)(void));

    /**
     * d-tor of the thread
     */
    ~Thread();

    /**
     * @return the id of the current thread
     */
    int getId() const;

    /**
     * @return the priority of the current thread
     */
    int getPriority() const;

    /**
     * @return the number of quantoms of this thread
     */
    int getNumOfQuantums() const;

    /**
     * Increase the number of quantoms by one
     */
    void increaseNumOfQuantums();

    /**
     * @return the quantom of the current thread
     */
    int getQuantum() const;

    /**
     * @return the state of the current thread
     */
    State getState() const;

    /**
     * sets the priority of the current thread
     * @param priority - new priority to set.
     */
    void setPriority(int priority);

    /**
     * sets the state of the current thread
     * @param state new state to set in the current thread
     */
    void setState(State state);

    /**
     * operator == of the Thread class. if the two Threads has the same id it returns true, else it retuns false
     */
    bool operator==(const Thread & other);

    char* stack [STACK_SIZE];
private:
    int _id;
    int _priority;
    int _quantum;
    State _state;
    void (*_f)(void);
    int _numOfQuantums;

};

#endif //EX2_THREAD_HPP
