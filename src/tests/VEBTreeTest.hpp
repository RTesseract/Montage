#ifndef VEBTREE_TEST_HPP
#define VEBTREE_TEST_HPP

#include "TestConfig.hpp"

template <class K = int, class V = int>
class VEBTreeTest : public Test {
public:
    int p_inserts, p_deletes, p_members, p_predecessors, p_successors;
    int range, prefill;

    VEBTreeTest(int p_inserts, int p_deletes, int p_members, int p_predecessors, int p_successors, int range, int prefill = 0) {
        assert(p_inserts + p_deletes + p_members + p_predecessors + p_successors == 100);
        this->p_inserts = p_inserts;
        this->p_deletes = p_deletes;
        this->p_members = p_members;
        this->p_predecessors = p_predecessors;
        this->p_successors = p_successors;
        this->range = range;
        this->prefill = prefill;
    }

    void allocRideable(GlobalTestConfig* gtc);
    void doPrefill(GlobalTestConfig* gtc);
    void operation(uint64_t key, int op, int tid);

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc) override {}

    void init(GlobalTestConfig* gtc) override {
        allocRideable(gtc);
        if (gtc->verbose)
            printf("Inserts: %d, Deletes: %d, Members: %d, Predecessors: %d, Successors: %d\n", p_inserts, p_deletes, p_members, p_predecessors, p_successors);
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
