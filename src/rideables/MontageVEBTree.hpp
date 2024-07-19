#ifndef MONTAGE_VEBTREE_HPP
#define MONTAGE_VEBTREE_HPP

#include "Rideable.hpp"
#include "veb_kv_cutoffWithLeafType/veb_kv_cutoffWithLeafType.h"
#include "optional.hpp"

template <class K, class V>
class MontageVEBTree: public Rideable {
public:
    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc) {}
};

template <class T = int>
class MontageVEBTreeFactory: public RideableFactory {
public:
    Rideable* build(GlobalTestConfig* gtc) override {
        return new MontageVEBTree<T, T>();
    }
};

#endif // MONTAGE_VEBTREE_HPP
