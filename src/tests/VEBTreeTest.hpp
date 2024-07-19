#ifndef VEBTREE_TEST_HPP
#define VEBTREE_TEST_HPP

#include "TestConfig.hpp"
#include "MontageVEBTree.hpp"

template <class K = int, class V = int>
class VEBTreeTest : public Test {
public:
    MontageVEBTree<K, V>* m;
    int p_inserts, p_upserts, p_deletes, p_members, p_predecessors, p_successors;
    int prop_inserts, prop_upserts, prop_deletes, prop_members, prop_predecessors, prop_successors;
    int range, prefill;

    inline K fromInt(uint64_t i) {
        return (K)i;
    }

    VEBTreeTest(int p_inserts, int p_upserts, int p_deletes, int p_members, int p_predecessors, int p_successors, int range, int prefill = 0) {
        assert(p_inserts + p_deletes + p_members + p_predecessors + p_successors == 100);
        this->p_inserts = p_inserts;
        this->p_upserts = p_upserts;
        this->p_deletes = p_deletes;
        this->p_members = p_members;
        this->p_predecessors = p_predecessors;
        this->p_successors = p_successors;
        this->range = range;

        globalRange = range;
        this->prefill = prefill;

        int sum = p_inserts;
        prop_inserts = sum;
        sum += p_upserts;
        prop_upserts = sum;
        sum += p_deletes;
        prop_deletes = sum;
        sum += p_members;
        prop_members = sum;
        sum += p_predecessors;
        prop_predecessors = sum;
        sum += p_successors;
        prop_successors = sum;
    }

    void allocRideable(GlobalTestConfig* gtc) {
        Rideable *ptr = gtc->allocRideable();
        m = dynamic_cast<MontageVEBTree<K, V>*>(ptr);
        if (!m) errexit("VEBTreeTest must be run on MontageVEBTree<K,V> type object.");
    }

    void doPrefill(GlobalTestConfig* gtc) {
        for (int i = 0; i < prefill; i++)
            m->insert(fromInt(i), fromInt(i), 0);
        if (gtc->verbose)
            printf("Prefilled %d elements\n", prefill);
        Recoverable* rec = dynamic_cast<Recoverable*>(m);
        if (rec) rec->sync();
    }

    void operation(uint64_t key, int op, int tid) {
        K k = fromInt(key);
        V v = k;
        if (op < prop_inserts)
            m->insert(k, v, tid);
        else if (op < prop_upserts)
            m->upsert(k, v, tid);
        else if (op < prop_deletes)
            m->del(k, tid);
        else if (op < prop_members)
            m->member(k, tid);
        else if (op < prop_predecessors)
            m->predecessor(k, tid);
        else
            m->successor(k, tid);
    }

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc) override {
        m->init_thread(gtc, ltc);
    }

    void init(GlobalTestConfig* gtc) override {
        allocRideable(gtc);
        if (gtc->verbose)
            printf("Inserts: %d, Upserts: %d, Deletes: %d, Members: %d, Predecessors: %d, Successors: %d\n", p_inserts, p_upserts, p_deletes, p_members, p_predecessors, p_successors);
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc) override {
        auto time_up = gtc->finish;

        int ops = 0;
        uint64_t r = ltc->seed;
        std::mt19937_64 gen_k(r);
        std::mt19937_64 gen_p(r+1);

        int tid = ltc->tid;

        // atomic_thread_fence(std::memory_order_acq_rel);
        // broker->threadInit(gtc,ltc);
        auto now = std::chrono::high_resolution_clock::now();

        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0) {
            r = abs((long)gen_k()%range);
            // r = abs(rand_nums[(k_idx++)%1000]%range);
            int p = abs((long)gen_p()%100);
            // int p = abs(rand_nums[(p_idx++)%1000]%100);

            operation(r, p, tid);
            ops++;
            if (ops % 512 == 0){
                now = std::chrono::high_resolution_clock::now();
            }
            // TODO: replace this with __rdtsc
            // or use hrtimer (high-resolution timer API in linux.)
        }
        return ops;
    }

    void cleanup(GlobalTestConfig* gtc) override {}
};


#endif // VEBTREE_TEST_HPP
