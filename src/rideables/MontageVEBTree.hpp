#ifndef MONTAGEVEBTREE_HPP
#define MONTAGEVEBTREE_HPP

#include "RSet.hpp"
#include "vEB/veb_k_naive.h"

inline int veb_u;

template <class K>
class MontageVEBTree : public RSet<K> {
public:
    VebKNaive *root;

    MontageVEBTree(GlobalTestConfig* gtc) {
        if (veb_u < 2)
            errexit("veb_u must be at least 2");
        root = new VebKNaive(veb_u);
    }

    ~MontageVEBTree() {
        delete root;
    }

    bool insert(K key, int tid) override {
        return root->insertDriver(tid, key);
    }

    bool remove(K key, int tid) override {
        return root->delDriver(tid, key);
    }

    bool get(K key, int tid) override {
        return root->memberDriver(tid, key);
    }

    void put(K key, int tid) override {}
};

template <class T>
class MontageVEBTreeFactory : public RideableFactory {
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageVEBTree<T>(gtc);
    }
};

#endif // MONTAGEVEBTREE_HPP
