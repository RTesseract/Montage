#ifndef HTM_HASHTABLE_HPP
#define HTM_HASHTABLE_HPP

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include "Recoverable.hpp"
#include "HTM.hpp"
#include <omp.h>

template<typename K, typename V, size_t idxSize=1000000>
class HTMHashTable : public RMap<K,V>, public Recoverable{
public:

    class Payload : public pds::PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        Payload(const Payload& oth): pds::PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    }__attribute__((aligned(CACHELINE_SIZE)));

    struct ListNode{
        HTMHashTable* ds;
        // Transient-to-persistent pointer
        Payload* payload = nullptr;
        // Transient-to-transient pointers
        ListNode* next = nullptr;
        ListNode(){}
        ListNode(HTMHashTable* ds_, K key, V val): ds(ds_){
            payload = ds->pnew<Payload>(key, val);
        }
        ListNode(HTMHashTable* ds_, Payload* _payload) : ds(ds_), payload(_payload) {} // for recovery
        K get_key(){
            assert(payload!=nullptr && "payload shouldn't be null");
            // old-see-new never happens for locking ds
            return (K)payload->get_unsafe_key(ds);
        }
        V get_val(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (V)payload->get_unsafe_val(ds);
        }
        void set_val(V v){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload = payload->set_val(ds, v);
        }
        ~ListNode(){
            if (payload){
                ds->pdelete(payload);
            }
        }
    }__attribute__((aligned(CACHELINE_SIZE)));
    struct Bucket{
        GlobalLock lock;
        ListNode head;
        Bucket():head(){};
    }__attribute__((aligned(CACHELINE_SIZE)));

    std::hash<K> hash_fn;
    Bucket buckets[idxSize];
    GlobalTestConfig* gtc;
    HTMHashTable(GlobalTestConfig* gtc_): Recoverable(gtc_), gtc(gtc_){
        if (get_recovered_pblks()) {
            recover();
        }
    };

    ~HTMHashTable() {
        recover_mode(); // PDELETE --> noop
        // clear transient structures.
        clear();
        online_mode(); // re-enable PDELETE.
    }

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    optional<V> get(K key, int tid){
        /* section begin */
        size_t idx=hash_fn(key)%idxSize;
        MontageOpHolderReadOnly(this);
        ListNode* curr;
        /* section end */
        int htmRetriesLeft = htmMaxRetriesLeft;
        unsigned int htmStatus;
        htmRetry:
        htmStatus = _xbegin();
        if (htmStatus == _XBEGIN_STARTED)
        {
            if (buckets[idx].lock.isLocked()) _xabort(_XABORT_EXPLICIT);
            /* section begin */
            curr = buckets[idx].head.next;
            while(curr){
                if (curr->get_key() == key){
                    _xend();
                    return curr->get_val();
                }
                curr = curr->next;
            }
            /* section end */
            _xend();
        }
        else
        {
            while(buckets[idx].lock.isLocked()) htmWait();
            if (--htmRetriesLeft > 0) goto htmRetry;
            buckets[idx].lock.lock();
            /* section begin */
            curr = buckets[idx].head.next;
            while(curr){
                if (curr->get_key() == key){
                    buckets[idx].lock.unlock();
                    return curr->get_val();
                }
                curr = curr->next;
            }
            /* section end */
            buckets[idx].lock.unlock();
        }
        return {};
    }

    optional<V> put(K key, V val, int tid){
        /* section begin */
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(this, key, val);
        MontageOpHolder _holder(this);
        ListNode *curr, *prev;
        K curr_key;
        optional<V> ret;
        /* section end */
        int htmRetriesLeft = htmMaxRetriesLeft;
        unsigned int htmStatus;
        htmRetry:
        htmStatus = _xbegin();
        if (htmStatus == _XBEGIN_STARTED)
        {
            if (buckets[idx].lock.isLocked()) _xabort(_XABORT_EXPLICIT);
            /* section begin */
            curr = buckets[idx].head.next;
            prev = &buckets[idx].head;
            while(curr){
                curr_key = curr->get_key();
                if (curr_key == key){
                    ret = curr->get_val();
                    curr->set_val(val);
                    delete new_node;
                    _xend();
                    return ret;
                } else if (curr_key > key){
                    new_node->next = curr;
                    prev->next = new_node;
                    _xend();
                    return {};
                } else {
                    prev = curr;
                    curr = curr->next;
                }
            }
            prev->next = new_node;
            /* section end */
            _xend();
        }
        else
        {
            while(buckets[idx].lock.isLocked()) htmWait();
            if (--htmRetriesLeft > 0) goto htmRetry;
            buckets[idx].lock.lock();
            /* section begin */
            curr = buckets[idx].head.next;
            prev = &buckets[idx].head;
            while(curr){
                curr_key = curr->get_key();
                if (curr_key == key){
                    ret = curr->get_val();
                    curr->set_val(val);
                    delete new_node;
                    buckets[idx].lock.unlock();
                    return ret;
                } else if (curr_key > key){
                    new_node->next = curr;
                    prev->next = new_node;
                    buckets[idx].lock.unlock();
                    return {};
                } else {
                    prev = curr;
                    curr = curr->next;
                }
            }
            prev->next = new_node;
            /* section end */
            buckets[idx].lock.unlock();
        }
        return {};
    }

    bool insert(K key, V val, int tid){
        /* section begin */
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(this, key, val);
        MontageOpHolder _holder(this);
        ListNode *curr, *prev;
        K curr_key;
        /* section end */
        int htmRetriesLeft = htmMaxRetriesLeft;
        unsigned int htmStatus;
        htmRetry:
        htmStatus = _xbegin();
        if (htmStatus == _XBEGIN_STARTED)
        {
            if (buckets[idx].lock.isLocked()) _xabort(_XABORT_EXPLICIT);
            /* section begin */
            curr = buckets[idx].head.next;
            prev = &buckets[idx].head;
            while(curr){
                curr_key = curr->get_key();
                if (curr_key == key){
                    delete new_node;
                    _xend();
                    return false;
                } else if (curr_key > key){
                    new_node->next = curr;
                    prev->next = new_node;
                    _xend();
                    return true;
                } else {
                    prev = curr;
                    curr = curr->next;
                }
            }
            prev->next = new_node;
            /* section end */
            _xend();
        }
        else
        {
            while(buckets[idx].lock.isLocked()) htmWait();
            if (--htmRetriesLeft > 0) goto htmRetry;
            buckets[idx].lock.lock();
            /* section begin */
            curr = buckets[idx].head.next;
            prev = &buckets[idx].head;
            while(curr){
                curr_key = curr->get_key();
                if (curr_key == key){
                    delete new_node;
                    buckets[idx].lock.unlock();
                    return false;
                } else if (curr_key > key){
                    new_node->next = curr;
                    prev->next = new_node;
                    buckets[idx].lock.unlock();
                    return true;
                } else {
                    prev = curr;
                    curr = curr->next;
                }
            }
            prev->next = new_node;
            /* section end */
            buckets[idx].lock.unlock();
        }
        return true;
    }

    optional<V> replace(K key, V val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<V> remove(K key, int tid){
        /* section begin */
        size_t idx=hash_fn(key)%idxSize;
        MontageOpHolder _holder(this);
        ListNode *curr, *prev;
        K curr_key;
        optional<V> ret;
        /* section end */
        int htmRetriesLeft = htmMaxRetriesLeft;
        unsigned int htmStatus;
        htmRetry:
        htmStatus = _xbegin();
        if (htmStatus == _XBEGIN_STARTED)
        {
            if (buckets[idx].lock.isLocked()) _xabort(_XABORT_EXPLICIT);
            /* section begin */
            curr = buckets[idx].head.next;
            prev = &buckets[idx].head;
            while(curr){
                curr_key = curr->get_key();
                if (curr_key == key){
                    ret = curr->get_val();
                    prev->next = curr->next;
                    delete(curr);
                    _xend();
                    return ret;
                } else if (curr_key > key){
                    _xend();
                    return {};
                } else {
                    prev = curr;
                    curr = curr->next;
                }
            }
            /* section end */
            _xend();
        }
        else
        {
            while(buckets[idx].lock.isLocked()) htmWait();
            if (--htmRetriesLeft > 0) goto htmRetry;
            buckets[idx].lock.lock();
            /* section begin */
            curr = buckets[idx].head.next;
            prev = &buckets[idx].head;
            while(curr){
                curr_key = curr->get_key();
                if (curr_key == key){
                    ret = curr->get_val();
                    prev->next = curr->next;
                    delete(curr);
                    buckets[idx].lock.unlock();
                    return ret;
                } else if (curr_key > key){
                    buckets[idx].lock.unlock();
                    return {};
                } else {
                    prev = curr;
                    curr = curr->next;
                }
            }
            /* section end */
            buckets[idx].lock.unlock();
        }
        return {};
    }

    void clear(){
        for (uint64_t i = 0; i < idxSize; i++){
            ListNode* curr = buckets[i].head.next;
            ListNode* next = nullptr;
            while(curr){
                next = curr->next;
                delete curr;
                curr = next;
            }
            buckets[i].head.next = nullptr;
        }
    }


    int recover(){
        std::unordered_map<uint64_t, pds::PBlk*>* recovered = get_recovered_pblks();
        assert(recovered);

        int rec_cnt = 0;
        int rec_thd = gtc->task_num;
        if (gtc->checkEnv("RecoverThread")){
            rec_thd = stoi(gtc->getEnv("RecoverThread"));
        }
        std::vector<Payload*> payloadVector;
        payloadVector.reserve(recovered->size());
        auto begin = chrono::high_resolution_clock::now();
        for (auto itr = recovered->begin(); itr != recovered->end(); itr++){
            rec_cnt++;
            Payload* payload = reinterpret_cast<Payload*>(itr->second);
                        payloadVector.push_back(payload);
        }
        auto end = chrono::high_resolution_clock::now();
        auto dur = end - begin;
        auto dur_ms_vec = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_vec << "ms building vector" << std::endl;
        begin = chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;

        ListNode* new_node;
        size_t idx;
        K key;
        int htmRetriesLeft;
        unsigned int htmStatus;
        ListNode *curr, *prev;
        K curr_key;

        for (int rec_tid = 0; rec_tid < rec_thd; rec_tid++) {
            workers.emplace_back(std::thread([&, rec_tid]() {
                Recoverable::init_thread(rec_tid);
                hwloc_set_cpubind(gtc->topology,
                                  gtc->affinities[rec_tid]->cpuset,
                                  HWLOC_CPUBIND_THREAD);
                for (size_t i = rec_tid; i < payloadVector.size(); i += rec_thd){
                    /* section begin */
                    new_node = new ListNode(this, payloadVector[i]);
                    key = new_node->get_key();
                    idx = hash_fn(key) % idxSize;
                    /* section end */
                    htmRetriesLeft = htmMaxRetriesLeft;
                    htmRetry:
                    htmStatus = _xbegin();
                    if (htmStatus == _XBEGIN_STARTED)
                    {
                        if (buckets[idx].lock.isLocked()) _xabort(_XABORT_EXPLICIT);
                        /* section begin */
                        curr = buckets[idx].head.next;
                        prev = &buckets[idx].head;
                        while (curr) {
                            curr_key = curr->get_key();
                            if (curr_key == key) {
                                _xend();
                                errexit("conflicting keys recovered.");
                            } else if (curr_key > key) {
                                new_node->next = curr;
                                prev->next = new_node;
                                _xend();
                                break;
                            } else {
                                prev = curr;
                                curr = curr->next;
                            }
                        }
                        prev->next = new_node;
                        /* section end */
                        _xend();
                    }
                    else
                    {
                        while(buckets[idx].lock.isLocked()) htmWait();
                        if (--htmRetriesLeft > 0) goto htmRetry;
                        buckets[idx].lock.lock();
                        /* section begin */
                        curr = buckets[idx].head.next;
                        prev = &buckets[idx].head;
                        while (curr) {
                            curr_key = curr->get_key();
                            if (curr_key == key) {
                                buckets[idx].lock.unlock();
                                errexit("conflicting keys recovered.");
                            } else if (curr_key > key) {
                                new_node->next = curr;
                                prev->next = new_node;
                                buckets[idx].lock.unlock();
                                break;
                            } else {
                                prev = curr;
                                curr = curr->next;
                            }
                        }
                        prev->next = new_node;
                        /* section end */
                        buckets[idx].lock.unlock();
                    }
                }
            }));
        }
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        auto dur_ms_ins = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_ins << "ms inserting(" << recovered->size() << ")" << std::endl;
        return rec_cnt;
    }
};

template <class T> 
class HTMHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new HTMHashTable<T,T>(gtc);
    }
};

/* Specialization for strings */
#include <string>
#include "InPlaceString.hpp"
template <>
class HTMHashTable<std::string, std::string, 1000000>::Payload : public pds::PBlk{
    GENERATE_FIELD(pds::InPlaceString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(pds::InPlaceString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(const std::string& k, const std::string& v) : m_key(this, k), m_val(this, v){}
    Payload(const Payload& oth) : pds::PBlk(oth), m_key(this, oth.m_key), m_val(this, oth.m_val){}
    void persist(){}
};

#endif
