#ifndef MONTAGE_VEBTREE_HPP
#define MONTAGE_VEBTREE_HPP

#include "TestConfig.hpp"
#include "Rideable.hpp"
#include "veb_kv_cutoffWithLeafType/veb_kv_cutoffWithLeafType.h"
#include "optional.hpp"

inline int globalRange;

template <class K, class V>
class MontageVEBTree: public Rideable {
private:
    VebKVInternal<K> *root;
public:
    MontageVEBTree(i64 u = globalRange) {
        root = new VebKVInternal<K>(u);
    }

    inline i64 toInt(K key) {
        return (i64)key;
    }

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc) {
        root->initThread(ltc->tid);
    }

    bool member(K key, const int tid) {
        return root->memberDriver(tid, toInt(key));
    }

    bool upsert(K key, V value, const int tid) {
        return root->upsertDriver(tid, toInt(key), value);
    }

    bool insert(K key, V value, const int tid) {
        return root->insertIfAbsentDriver(tid, toInt(key), value);
    }

    bool del(K key, const int tid) {
        return root->delDriver(tid, toInt(key));
    }

    std::pair<i64, V> successor(K key, const int tid) {
        return root->successorDriver(tid, toInt(key));
    }

    std::pair<i64, V> predecessor(K key, const int tid) {
        return root->predecessorDriver(tid, toInt(key));
    }
};

template <class T = int>
class MontageVEBTreeFactory: public RideableFactory {
public:
    Rideable* build(GlobalTestConfig* gtc) override {
        return new MontageVEBTree<T, T>();
    }
};

#endif // MONTAGE_VEBTREE_HPP
