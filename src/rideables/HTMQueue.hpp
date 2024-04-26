#ifndef HTM_QUEUE_P
#define HTM_QUEUE_P

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RQueue.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"
#include "Recoverable.hpp"
#include "Recoverable.hpp"
#include "HTM.hpp"


template<typename T>
class HTMQueue : public RQueue<T>, public Recoverable{
public:
    class Payload : public pds::PBlk{
        GENERATE_FIELD(T, val, Payload);
        GENERATE_FIELD(uint64_t, sn, Payload); 
    public:
        Payload(){}
        Payload(T v, uint64_t n): m_val(v), m_sn(n){}
        Payload(const Payload& oth): PBlk(oth), m_sn(0), m_val(oth.m_val){}
        void persist(){}
    };

private:
    struct Node{
        HTMQueue* ds;
        Node* next;
        Payload* payload;
        T val; // for debug purpose

        Node(): next(nullptr), payload(nullptr){}; 
        // Node(): next(nullptr){}; 
        Node(HTMQueue* ds_, T v, uint64_t n=0): 
            ds(ds_), next(nullptr), payload(ds_->pnew<Payload>(v, n)), val(v){};
        // Node(T v, uint64_t n): next(nullptr), val(v){};

        void set_sn(uint64_t s){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload->set_unsafe_sn(ds, s);
        }
        T get_val(){
            assert(payload!=nullptr && "payload shouldn't be null");
            // old-see-new never happens for locking ds
            return (T)payload->get_unsafe_val(ds);
            // return val;
        }
        ~Node(){
            ds->pdelete(payload);
        }
    };

public:
    uint64_t global_sn;

private:
    // dequeue pops node from head
    Node* head;
    // enqueue pushes node to tail
    Node* tail;
    GlobalLock lock;

public:
    HTMQueue(GlobalTestConfig* gtc): 
        Recoverable(gtc), global_sn(0), head(nullptr), tail(nullptr){
    }

    ~HTMQueue(){};

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(){
        errexit("recover of HTMQueue not implemented.");
        return 0;
    }

    void enqueue(T val, int tid);
    optional<T> dequeue(int tid);
};

template<typename T>
void HTMQueue<T>::enqueue(T val, int tid){
    /* section begin */
    MontageOpHolder _holder(this);
    Node* new_node = new Node(this, val);
    /* section end */
    int htmRetriesLeft = htmMaxRetriesLeft;
    unsigned int htmStatus;
    htmRetry:
    htmStatus = _xbegin();
    if (htmStatus == _XBEGIN_STARTED)
    {
        if (lock.isLocked()) _xabort(_XABORT_EXPLICIT);
        /* section begin */
        new_node->set_sn(global_sn);
        global_sn++;
        if(tail == nullptr) {
            head = tail = new_node;
            _xend();
            return;
        }
        tail->next = new_node;
        tail = new_node;
        /* section end */
        _xend();
    }
    else
    {
        htmSpinWait(lock);
        if (--htmRetriesLeft > 0) goto htmRetry;
        lock.lock();
        /* section begin */
        new_node->set_sn(global_sn);
        global_sn++;
        if(tail == nullptr) {
            head = tail = new_node;
            lock.unlock();
            return;
        }
        tail->next = new_node;
        tail = new_node;
        /* section end */
        lock.unlock();
    }
}

template<typename T>
optional<T> HTMQueue<T>::dequeue(int tid){
    /* section begin */
    MontageOpHolder _holder(this);
    optional<T> res = {};
    /* section end */
    int htmRetriesLeft = htmMaxRetriesLeft;
    unsigned int htmStatus;
    htmRetry:
    htmStatus = _xbegin();
    if (htmStatus == _XBEGIN_STARTED)
    {
        if (lock.isLocked()) _xabort(_XABORT_EXPLICIT);
        /* section begin */
        if(head == nullptr) {
            _xend();
            return res;
        }
        Node* tmp = head;
        head = head->next;
        if(head == nullptr) {
            tail = nullptr;
        }
        /* section end */
        _xend();
        /* section begin */
        res = tmp->get_val();
        delete(tmp);
        /* section end */
    }
    else
    {
        htmSpinWait(lock);
        if (--htmRetriesLeft > 0) goto htmRetry;
        lock.lock();
        /* section begin */
        if(head == nullptr) {
            lock.unlock();
            return res;
        }
        Node* tmp = head;
        res = tmp->get_val();
        head = head->next;
        if(head == nullptr) {
            tail = nullptr;
        }
        delete(tmp);
        /* section end */
        lock.unlock();
    }
    return res;
}

template <class T> 
class HTMQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new HTMQueue<T>(gtc);
    }
};

/* Specialization for strings */
#include <string>
#include "InPlaceString.hpp"
template <>
class HTMQueue<std::string>::Payload : public pds::PBlk{
    GENERATE_FIELD(pds::InPlaceString<TESTS_VAL_SIZE>, val, Payload);
    GENERATE_FIELD(uint64_t, sn, Payload);

public:
    Payload(std::string v, uint64_t n) : m_val(this, v), m_sn(n){}
    Payload(const Payload& oth) : pds::PBlk(oth), m_val(this, oth.m_val), m_sn(oth.m_sn){}
    void persist(){}
};

#endif
