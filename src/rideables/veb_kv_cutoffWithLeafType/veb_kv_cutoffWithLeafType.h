#ifndef VEB_KV_CUTOFFWITHLEAFTYPE_IMPL_H
#define VEB_KV_CUTOFFWITHLEAFTYPE_IMPL_H

#include <immintrin.h>
#include <stdio.h>

// #include <bit>
#include <bitset>
#include <iostream>
#include <map>
#include <vector>

#include "plaf.h"
#include "locks_impl.h"
#include "veb_global.h"

using namespace std;

PAD;
volatile int globalLock = 0;
PAD;
thread_local map<i64, UniverseInfo> kMap;   // for key-only structures
thread_local map<i64, UniverseInfo> kvMap;  // for kv structures

thread_local map<i64, vector<void *>> kPool;  // for key-only structures
thread_local vector<i64> kToRefill;           // for key-only structures
thread_local vector<void *> kToReclaim;       // for key-only structures

thread_local map<i64, vector<void *>> kvPool;  // for kv structures
thread_local vector<i64> kvToRefill;           // for kv structures
thread_local vector<void *> kvToReclaim;       // for kv structures

#define CUTOFF 64
#define CUTOFF_POWER 6
#define COMMA ,

// checks the LSB to see if it's a leaf or an internal
#define IS_LEAF(ptr) ((uintptr_t)(ptr)&1ULL)

// sets the lsb to zero to make the pointer valid (after knowing it's a leaf)
#define PTR_TO_KLEAF(ptr) ((VebKLeaf *)((uintptr_t)(ptr) & ~1ULL))
// doesn't do anything, just casts from void* to approprate type
#define PTR_TO_KINTERNAL(ptr) ((VebKInternal *)(ptr))
// sets the lsb to 1 to make the pointer a leaf
#define MARK_PTR_AS_KLEAF(ptr) ((VebKInternal *)((uintptr_t)(ptr) | 1ULL))

// sets the lsb to zero to make the pointer valid (after knowing it's a leaf)
#define PTR_TO_KVLEAF(ptr) ((VebKVLeaf<V> *)((uintptr_t)(ptr) & ~1ULL))
// doesn't do anything, just casts from void* to approprate type
#define PTR_TO_KVINTERNAL(ptr) ((VebKVInternal<V> *)(ptr))
// sets the lsb to 1 to make the pointer a leaf
#define MARK_PTR_AS_KVLEAF(ptr) ((VebKVInternal<V> *)((uintptr_t)(ptr) | 1ULL))

#define KCALL_VARIADIC(ptr, func, args)      \
    if (IS_LEAF((ptr))) {                    \
        PTR_TO_KLEAF((ptr))->func(args);     \
    } else {                                 \
        PTR_TO_KINTERNAL((ptr))->func(args); \
    }

#define KVCALL_VARIADIC(ptr, func, args)      \
    if (IS_LEAF((ptr))) {                     \
        PTR_TO_KVLEAF((ptr))->func(args);     \
    } else {                                  \
        PTR_TO_KVINTERNAL((ptr))->func(args); \
    }

namespace custom {
    constexpr int countl_zero(ui64 x) noexcept {
        return x == 0 ? 64 : __builtin_clzl(x);
    }
    constexpr int countr_zero(ui64 x) noexcept {
        return x == 0 ? 64 : __builtin_ctzl(x);
    }
}

class VebKLeaf {
public:
    i64 u;
    volatile i64 min;
    volatile i64 max;
    ui64 bitmap;

    VebKLeaf(i64 u) {
        this->u = u;
        this->min = -1;
        this->max = -1;
        this->bitmap = 0ULL;
    }

    void makeMeReusable() {
        this->min = -1;
        this->max = -1;
        this->bitmap = 0ULL;
    }

    bool member(i64 x) {
        if (x == this->min || x == this->max) {
            return true;
        }
        return this->bitmap & (1ULL << x);
    }

    i64 successor(i64 x) {
        // TODO: this is never reached if I appropriately null-check in the parent
        // if the leaf is empty, return -1
        if (this->min == -1) return -1;

        // if the query is smaller than the minimum, return the minimum
        if (x < this->min) {
            return this->min;
        }

        if (x >= this->max) {
            return -1;
        }

        // if there's only one key in this leaf, and the query is not smaller than it (checked in the if statement above), return -1
        if (this->min == this->max) {
            return -1;
        }

        // If I arrive here, there's at least 2 elements in this leaf
        // There is at least one bit set in the bitmap
        // x >= min, so "min" cannot be the answer (successor), and it's not set in the bitmap anyways
        ui64 tempBitmap = this->bitmap;
        ui64 mask = 0xFFFFFFFFFFFFFFFFu << (x + 1);

        i64 consecutiveZerosOnRight = custom::countr_zero(tempBitmap & mask);

        // TODO: fix constant
        return consecutiveZerosOnRight == 64 ? -1 : consecutiveZerosOnRight;
    }

    i64 predecessor(i64 x) {
        // if the leaf is empty, return -1
        // TODO: this is never reached if I appropriately null-check in the parent
        if (this->min == -1) return -1;

        // arriving here means that this->max IS NOT -1, because there's at least one element in the leaf
        // if the query is larger than the maximum, return the maximum
        if (x > this->max) {
            return this->max;
        }

        if (x <= this->min) {
            return -1;
        }

        // if there's only one key in this leaf, and the query is not larger than it (checked in the if statement above), return -1
        if (this->min == this->max) {
            return -1;
        }

        // if we arrive here, there is at least 2 elements in this leaf
        // the query is <= this->max, and the max IS set in the bitmap
        // the final result depends on two cases:
        // 1. there is 2 elements => the result will be the min, which is not in the bitmap
        // 2. there is 3 or more elements => the result will be some bit set somewhere
        ui64 tempBitmap = this->bitmap;
        ui64 mask = ~(0xFFFFFFFFFFFFFFFFu << (x));

        int consecutiveZerosOnLeft = custom::countl_zero(tempBitmap & mask);
        // TODO: fix the constant here
        int firstSetBitBeforeMe = 64 - consecutiveZerosOnLeft - 1;
        return firstSetBitBeforeMe == -1 ? this->min : firstSetBitBeforeMe;
    }

    void insertToEmptyVEB(i64 x) {
        this->min = x;
        this->max = x;
    }

    bool insert(i64 x) {
        // AVOID RE-INSERTING THE MINIMUM OR THE MAXIMUM INSIDE THE CLUSTER
        if (x == this->min || x == this->max) {
            return false;
        }

        // easy case: tree is empty
        if (this->min == -1) {
            this->insertToEmptyVEB(x);
            return true;
        }

        bool inserted = false;

        if (x < this->min) {
            // neeed to insert the old min into the bit vector (the minimum is never stored in the bit vector to be compliant with veb requirements)
            inserted = true;
            i64 temp = x;
            x = this->min;
            this->min = temp;
        }

        // now insert the key
        inserted = ((this->bitmap) & (1ULL << x)) == 0;
        this->bitmap = this->bitmap | (1ULL << x);

        if (x > this->max) {
            this->max = x;
        }

        return inserted;
    }

    bool del(i64 x) {
        // significantly improves throughput
        if (x > this->max || x < this->min) {
            return false;
        }

        // there's only 1 element in V
        // rare
        if (this->min == this->max) {
            if (this->min == x) {
                this->min = -1;
                this->max = -1;
                this->bitmap = 0ULL;
                return true;
            }
            return false;
        }

        bool erased = false;

        // if deleting the min, find the next min
        if (x == this->min) {
            // TODO: can be replaced with successor operation
            // int nextMin;
            // for (nextMin = x + 1; nextMin < this->u; ++nextMin) {
            //     if ((1ULL << nextMin) & this->bitmap) break;
            // }

            // I'm deleting the min, so there must be a set of consecutive zeros on the LSB side of bitmap
            int consecutiveZerosOnRight = custom::countr_zero(this->bitmap);

            this->min = consecutiveZerosOnRight;

            // now delete the new min from the bits
            ui64 mask = ~(1ULL << consecutiveZerosOnRight);
            this->bitmap = this->bitmap & mask;
            return true;
        }

        // if it's not the min, we can just unset the bit
        ui64 mask = (1ULL << x);
        if (!(this->bitmap & mask)) {
            // doesn't exist...
            return false;
        }

        // it exists, and we can delete it
        this->bitmap = this->bitmap & ~(mask);

        // if deleted the max element, gotta find the new max
        if (x == this->max) {
            // for (int bit = x - 1; bit >= 0; bit--) {
            //     if (this->bitmap & (1ULL << bit)) {
            //         this->max = bit;
            //         break;
            //     }
            //     if (bit == this->min) {  // special case, min is not in the bits
            //         this->max = this->min;
            //         break;
            //     }
            // }
            int consecutiveZerosOnLeft = custom::countl_zero(this->bitmap);
            // TODO: fix the constant here
            int newMax = 64 - consecutiveZerosOnLeft - 1;
            this->max = newMax == -1 ? this->min : newMax;
        }

        return true;
    }
};

class VebKInternal {
public:
    i64 u;
    VebKInternal **clusters;
    VebKInternal *summary;
    // PAD;
    volatile i64 min;
    volatile i64 max;
    // PAD;
    // Constructor

    VebKInternal(i64 u) {
        this->u = u;
        this->min = -1;
        this->max = -1;

        if (u <= CUTOFF) {
            this->clusters = 0ULL;
            this->summary = nullptr;
        } else {
            this->summary = nullptr;

            UniverseInfo ui;

            if (kMap.find(u) != kMap.end()) {
                ui = kMap[u];
            } else {
                ui = divide_node(u, CUTOFF_POWER, false);
            }

            this->clusters = new VebKInternal *[ui.nClusters];
            std::fill_n(this->clusters, ui.nClusters, nullptr);
        }
    }

    void makeMeReusable() {
        this->min = -1;
        this->max = -1;
        //
    }

    ~VebKInternal() {
        for (ui64 i = 0; i < kMap[this->u].nClusters; ++i) {
            if (this->clusters[i] != nullptr) {
                if (IS_LEAF(this->clusters[i])) {
                    auto cluster = PTR_TO_KLEAF(this->clusters[i]);
                    delete cluster;
                } else {
                    auto cluster = PTR_TO_KINTERNAL(this->clusters[i]);
                    delete cluster;
                }
            }
        }
        if (this->summary != nullptr) {
            if (IS_LEAF(this->summary)) {
                auto summary = PTR_TO_KLEAF(this->summary);
                delete summary;
            } else {
                auto summary = PTR_TO_KINTERNAL(this->summary);
                delete summary;
            }
        }
        delete[] this->clusters;
    }

    void populate_maps(i64 _u, bool isKV) {
        // printf("handling %ld as %s\n", _u, (isKV ? "KV" : "K"));
        UniverseInfo ui = divide_node(_u, CUTOFF_POWER, isKV);

        if (kMap.find(_u) == kMap.end()) {
            // printf("Inserting kMap[%ld]={lowBits: %ld, highBits: %ld}\n", _u, ui.lowBits, ui.highBits);
            kMap.insert(pair<i64, UniverseInfo>(_u, ui));
        }

        if (_u > CUTOFF) {
            populate_maps(ui.nClusters, false);
            populate_maps(ui.clusterSize, false);
        }
    }

    void deinitThread(const int tid) {
    }

    i64 getSumOfKeys() {
        i64 res = 0;
        for (i64 i = 0; i < this->u; ++i) {
            if (this->member(i)) {
                res += i;
            }
        }

        return res;
    }

    bool member(i64 x) {
        if (x == this->min || x == this->max) {
            return true;
        }

        // if (this->u <= CUTOFF) {
        //     ui64 curBits = (ui64)this->clusters;
        //     return curBits & (1ULL << x);
        // }

        // if (this->u == 2) {
        //     // if it's neither min nor max, and we can't recurse any further,
        //     we're done return false;
        // }
        UniverseInfo ui = kMap[this->u];

        i64 h = HIGH(x, ui);
        if (this->clusters[h] == nullptr) return false;

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KLEAF(this->clusters[h]);
            return cluster->member(LOW(x, ui));
        } else {
            auto cluster = PTR_TO_KINTERNAL(this->clusters[h]);
            return cluster->member(LOW(x, ui));
        }

        // return this->clusters[h]->member(LOW(x, ui));
    }

    i64 successor(i64 x) {
        // the query is smaller than the minimum => return the minimum
        if (this->min != -1 && x < this->min) {
            return this->min;
        }
        UniverseInfo ui = kMap[this->u];
        i64 h = HIGH(x, ui);

        if (this->clusters[h] != nullptr) {
            // the corresponding cluster exists... the successor "might" be there
            i64 l = LOW(x, ui);

            if (IS_LEAF(this->clusters[h])) {
                auto cluster = PTR_TO_KLEAF(this->clusters[h]);
                i64 maxLow = cluster->max;

                // TODO: do you have to check -1?
                if (maxLow != -1 && l < maxLow) {
                    i64 offset = cluster->successor(l);
                    return INDEX(h, offset, ui);
                }

            } else {
                auto cluster = PTR_TO_KINTERNAL(this->clusters[h]);
                i64 maxLow = cluster->max;

                // TODO: do you have to check -1?
                if (maxLow != -1 && l < maxLow) {
                    i64 offset = cluster->successor(l);
                    return INDEX(h, offset, ui);
                }
            }
        }

        // EITHER the corresponding cluster does not exist
        // OR it exists BUT the successor is not in there, so gotta look at other clusters (see the successor of h in the summary)

        // if there is no summary, there is no successor
        if (this->summary == nullptr) {
            return -1;
        }

        if (IS_LEAF(this->summary)) {
            auto summary = PTR_TO_KLEAF(this->summary);
            i64 succCluster = summary->successor(h);
            // nothing found in summary
            // => successor does not exist
            if (succCluster == -1) {
                return -1;
            }

            if (IS_LEAF(this->clusters[succCluster])) {
                // TODO: is this if statement necessary?
                // summary is leaf, so the cluster MUST be a leaf
                auto cluster = PTR_TO_KLEAF(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return INDEX(succCluster, offset, ui);
            } else {
                auto cluster = PTR_TO_KINTERNAL(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return INDEX(succCluster, offset, ui);
            }

        } else {
            auto summary = PTR_TO_KINTERNAL(this->summary);
            i64 succCluster = summary->successor(h);
            // nothing found in summary
            // => successor does not exist
            if (succCluster == -1) {
                return -1;
            }

            if (IS_LEAF(this->clusters[succCluster])) {
                auto cluster = PTR_TO_KLEAF(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return INDEX(succCluster, offset, ui);
            } else {
                auto cluster = PTR_TO_KINTERNAL(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return INDEX(succCluster, offset, ui);
            }
        }
    }

    i64 predecessor(i64 x) {
        // the query is bigger than the maximim => return the maximum

        // IMPORTANT: THIS IS A NICE LITTLE OPTIMIZATION, BUT SINCE I DON'T HAVE ACCESS TO THE
        // VALUE ASSOCIATED WITH this->max here (this is not a leaf node), MAYBE I CAN COMMENT IT OUT
        // SO THAT I TRAVERSE FURTHER AND FIND THE PREDECESSOR WHERE IT IS ACTUALLY STORED
        //
        // if (this->max != -1 && x > this->max) {
        //     return this->max;
        // }

        if (this->min == -1) {
            // empty node. it is RARELY called => ONLY on an empty root. We don't want to fail then
            // otherwise, this entire if statement can be omitted
            return -1;
        }

        if (x > this->max) {
            if (this->max == this->min) {
                // in this case, I can return the value safely and quickly because it's stored in minVal
                return this->max;
            } else {
                // in this case, I'll do nothing because I want to go further to find the actual node where the key is stored
            }
        }

        UniverseInfo ui = kMap[this->u];
        i64 h = HIGH(x, ui);

        if (this->clusters[h] != nullptr) {
            // the corresponding cluster exists... the predecessor "might" be there
            i64 l = LOW(x, ui);

            if (IS_LEAF(this->clusters[h])) {
                auto cluster = PTR_TO_KLEAF(this->clusters[h]);
                i64 minLow = cluster->min;

                if (minLow != -1 && minLow < l) {
                    i64 offset = cluster->predecessor(l);
                    return INDEX(h, offset, ui);
                }

            } else {
                auto cluster = PTR_TO_KINTERNAL(this->clusters[h]);
                i64 minLow = cluster->min;

                if (minLow != -1 && minLow < l) {
                    i64 offset = cluster->predecessor(l);
                    return INDEX(h, offset, ui);
                }
            }
        }

        // EITHER the corresponding cluster does not exist
        // OR it exists BUT the predecessor is not in there, so gotta look at other clusters (see the predecessor of h in the summary)

        // if there is no summary, there is no predecessor
        if (this->summary == nullptr) {
            return -1;
        }

        if (IS_LEAF(this->summary)) {
            auto summary = PTR_TO_KLEAF(this->summary);
            i64 predCluster = summary->predecessor(h);

            // IMPORTANT: different from sucecssor
            if (predCluster == -1) {
                // predecessor might be stored in a min field somewhere
                // and we didn't see it because min is not stored in any clusters
                if (this->min != -1 && x > this->min) {
                    return this->min;
                }
                return -1;
            }

            if (IS_LEAF(this->clusters[predCluster])) {
                auto cluster = PTR_TO_KLEAF(this->clusters[predCluster]);
                i64 offset = cluster->max;
                return INDEX(predCluster, offset, ui);
            } else {
                auto cluster = PTR_TO_KINTERNAL(this->clusters[predCluster]);
                i64 offset = cluster->max;
                return INDEX(predCluster, offset, ui);
            }
        } else {
            auto summary = PTR_TO_KINTERNAL(this->summary);
            i64 predCluster = summary->predecessor(h);

            // IMPORTANT: different from sucecssor
            if (predCluster == -1) {
                // predecessor might be stored in a min field somewhere
                // and we didn't see it because min is not stored in any clusters
                if (this->min != -1 && x > this->min) {
                    return this->min;
                }
                return -1;
            }

            if (IS_LEAF(this->clusters[predCluster])) {
                auto cluster = PTR_TO_KLEAF(this->clusters[predCluster]);
                i64 offset = cluster->max;
                return INDEX(predCluster, offset, ui);
            } else {
                auto cluster = PTR_TO_KINTERNAL(this->clusters[predCluster]);
                i64 offset = cluster->max;
                return INDEX(predCluster, offset, ui);
            }
        }
    }

    void allocateClusterIfNeeded(i64 h) {
        if (this->clusters[h] == nullptr) {
            i64 clusterU = kMap[this->u].clusterSize;
            void *newCluster = kPool[clusterU].back();
            if (clusterU <= CUTOFF) {
                this->clusters[h] = MARK_PTR_AS_KLEAF(newCluster);
            } else {
                this->clusters[h] = (VebKInternal *)newCluster;
            }
            kPool[kMap[this->u].clusterSize].pop_back();
            kToRefill.push_back(kMap[this->u].clusterSize);
        }
    }

    void allocateSummaryIfNeeded() {
        if (this->summary == nullptr) {
            i64 summaryU = kMap[this->u].nClusters;
            void *newSummary = kPool[summaryU].back();
            if (summaryU <= CUTOFF) {
                this->summary = MARK_PTR_AS_KLEAF(newSummary);
            } else {
                this->summary = (VebKInternal *)newSummary;
            }
            kPool[kMap[this->u].nClusters].pop_back();
            kToRefill.push_back(kMap[this->u].nClusters);
        }
    }

    void insertToEmptyVEB(i64 x) {
        this->min = x;
        this->max = x;
    }

    bool insert(i64 x) {
        // AVOID RE-INSERTING THE MINIMUM OR THE MAXIMUM INSIDE THE CLUSTER
        if (x == this->min || x == this->max) {
            return false;
        }

        // easy case: tree is empty
        if (this->min == -1) {
            this->insertToEmptyVEB(x);
            return true;
        }

        bool inserted;
        // x is our new minimum
        // set x as the new min, then insert the old min into the tree
        if (x < this->min) {
            // if it's less than the minimum, it doesn't exist in the tree
            // what about lower levels?
            // if it's smaller than the minimum in level l, it can't be
            // greater than any of the minumums on level l + k because the
            // top level minimum is the absolute minimum
            inserted = true;

            i64 temp = x;
            x = this->min;
            this->min = temp;
        }

        UniverseInfo ui = kMap[this->u];
        i64 h = HIGH(x, ui);
        i64 l = LOW(x, ui);

        this->allocateClusterIfNeeded(h);

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KLEAF(this->clusters[h]);
            if (cluster->min == -1) {
                this->allocateSummaryIfNeeded();
                KCALL_VARIADIC(this->summary, insert, h);
                cluster->insertToEmptyVEB(l);
                inserted = true;
            } else {
                inserted = cluster->insert(l);
            }

        } else {
            auto cluster = PTR_TO_KINTERNAL(this->clusters[h]);
            if (cluster->min == -1) {
                this->allocateSummaryIfNeeded();
                KCALL_VARIADIC(this->summary, insert, h);
                cluster->insertToEmptyVEB(l);
                inserted = true;
            } else {
                inserted = cluster->insert(l);
            }
        }

        if (x > this->max) {
            this->max = x;
        }

        return inserted;
    }

    bool del(i64 x) {
        // significantly improves throughput
        if (x > this->max || x < this->min) {
            return false;
        }
        // there's only 1 element in V
        // rare
        if (this->min == this->max) {
            if (this->min == x) {
                this->min = -1;
                this->max = -1;
                return true;
            }
            return false;
        }

        // from now on, V has at least 2 elements
        // if (this->u == 2) {
        //     // we know that V has 2 elems
        //     // we're at the base case
        //     // one of them must be x
        //     // delete it and set min and max accordingly
        //     this->min = 1 - x;
        //     this->max = this->min;

        //     return true;
        // }

        bool erased;

        // from now on, V has at least 2 elements and u >= 4
        UniverseInfo ui = kMap[this->u];

        // if deleting the min value,
        // set one of the elems as the new min
        // delete that element from inside the cluster
        if (x == this->min) {
            // no need to do nullptr check on summary because if there are
            // at least 2 elements in the structure, the summary has been
            // allocated
            if (IS_LEAF(this->summary)) {
                auto summary = PTR_TO_KLEAF(this->summary);
                i64 firstCluster = summary->min;
                // OLD COMMENTS, NOT APPLICABLE NOW:
                // summary is leaf, so cluster MUST be a leaf too because summarySize >= clusterSize always
                // NEW COMMENTS:
                // No assumptions!

                if (IS_LEAF(this->clusters[firstCluster])) {
                    auto cluster = PTR_TO_KLEAF(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;

                } else {
                    auto cluster = PTR_TO_KINTERNAL(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;
                }
            } else {
                auto summary = PTR_TO_KINTERNAL(this->summary);
                i64 firstCluster = summary->min;
                // summary is not a leaf, so cluster could be anything
                if (IS_LEAF(this->clusters[firstCluster])) {
                    auto cluster = PTR_TO_KLEAF(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;

                } else {
                    auto cluster = PTR_TO_KINTERNAL(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;
                }
            }
        }

        i64 h = HIGH(x, ui);
        //  this if statement only, handles the case for lazy veb
        // (prevents from seg faults)
        // reclamation remaining though
        if (this->clusters[h] == nullptr) {
            return false;
        }

        i64 l = LOW(x, ui);

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KLEAF(this->clusters[h]);
            erased = cluster->del(l);
            // if successfully deleted x and the cluster is empty now
            if (cluster->min == -1) {
                // free the memory
                // IMPORTANT: push back the marked pointer, not the one you unmarked to read the min of!
                kToReclaim.push_back(this->clusters[h]);
                this->clusters[h] = nullptr;

                // update summary so that it reflects the emptiness
                if (IS_LEAF(this->summary)) {
                    auto summary = PTR_TO_KLEAF(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the marked pointer, not the one you unmarked to read the min of!
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                } else {
                    auto summary = PTR_TO_KINTERNAL(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the unmarked pointer (even though it wasn't marked to begin with, it is good practice to avoid segfaults)

                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                }
            } else if (x == this->max) {
                this->max = INDEX(h, cluster->max, ui);
            }

        } else {
            auto cluster = PTR_TO_KINTERNAL(this->clusters[h]);
            erased = cluster->del(l);
            // OLD COMMENTS, NOT APPLICABLE NOW
            // cluster is internal, so the summary must be internal too
            // NEW COMMENTS: no assumptions anymore

            if (cluster->min == -1) {
                // IMPORTANT: push back the unmarked pointer (even though it wasn't marked to begin with, it is good practice to avoid segfaults)
                kToReclaim.push_back(this->clusters[h]);
                this->clusters[h] = nullptr;

                if (IS_LEAF(this->summary)) {
                    auto summary = PTR_TO_KLEAF(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the marked pointer, not the one you unmarked to read the min of!
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                } else {
                    auto summary = PTR_TO_KINTERNAL(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the unmarked pointer (even though it wasn't marked to begin with, it is good practice to avoid segfaults)
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                }
            } else if (x == this->max) {
                this->max = INDEX(h, cluster->max, ui);
            }
        }
        return erased;
    }

    bool memberDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(SEARCH, member, x);
        return retval;
    }

    bool insertDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(INSERT, insert, x);
        if (retval) {
            for (auto it = kToRefill.begin(); it != kToRefill.end(); ++it) {
                i64 neededSize = *it;
                if (neededSize > CUTOFF) {
                    while (kPool[neededSize].size() < POINTERS_PER_POOL) {
                        kPool[neededSize].push_back((void *)new VebKInternal(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_k, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                } else {
                    while (kPool[neededSize].size() < POINTERS_PER_POOL) {
                        kPool[neededSize].push_back((void *)new VebKLeaf(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_k, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                }
            }

            kToRefill.clear();
        }
        return retval;
    }

    bool delDriver(const int tid, i64 x) {
        bool retval = false;

        TLE(DELETE, del, x);

        if (retval) {
            for (auto it = kToReclaim.begin(); it != kToReclaim.end(); ++it) {
                void *temp = *it;
                if (IS_LEAF(temp)) {
                    auto node = PTR_TO_KLEAF(temp);
                    if (kPool[node->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
                        node->makeMeReusable();
                        kPool[node->u].push_back(temp);
                        // GSTATS_ADD_IX(tid, num_pool_free_k, 1, (int)ceil(log2(log2((double)node->u))));
                    } else {
                        delete node;
                    }

                } else {
                    auto node = PTR_TO_KINTERNAL(temp);
                    if (kPool[node->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
                        node->makeMeReusable();
                        kPool[node->u].push_back(temp);
                        // GSTATS_ADD_IX(tid, num_pool_free_k, 1, (int)ceil(log2(log2((double)node->u))));
                    } else {
                        delete node;
                    }
                }
            }
            kToReclaim.clear();
        }
        return retval;
    }

    i64 successorDriver(const int tid, i64 x) {
        i64 retval = -1;
        TLE(SUCCESSOR, successor, x);
        return retval;
    }

    i64 predecessorDriver(const int tid, i64 x) {
        i64 retval = -1;
        TLE(PREDECESSOR, predecessor, x);
        return retval;
    }
};

template <typename V>
class VebKVLeaf {
public:
    i64 u;
    volatile i64 min;
    volatile i64 max;
    ui64 bitmap;
    V minVal;
    V *vals;

    VebKVLeaf(i64 u) {
        this->u = u;
        this->min = -1;
        this->max = -1;
        this->bitmap = 0ULL;
        this->vals = new V[u];
    }

    void makeMeReusable() {
        this->min = -1;
        this->max = -1;
        this->bitmap = 0ULL;
    }

    bool member(i64 x) {
        if (x == this->min || x == this->max) {
            return true;
        }
        return this->bitmap & (1ULL << x);
    }

    std::pair<i64, V> successor(i64 x) {
        // TODO: this is never reached if I appropriately null-check in the parent
        // if the leaf is empty, return -1
        if (this->min == -1) std::make_pair(-1, nullptr);

        // if the query is smaller than the minimum, return the minimum
        if (x < this->min) {
            return std::make_pair(this->min, this->minVal);
        }

        if (x >= this->max) {
            std::make_pair(-1, nullptr);
        }

        // if there's only one key in this leaf, and the query is not smaller than it (checked in the if statement above), return -1
        if (this->min == this->max) {
            std::make_pair(-1, nullptr);
        }

        // If I arrive here, there's at least 2 elements in this leaf
        // There is at least one bit set in the bitmap
        // x >= min, so "min" cannot be the answer (successor), and it's not set in the bitmap anyways
        ui64 tempBitmap = this->bitmap;
        ui64 mask = 0xFFFFFFFFFFFFFFFFu << (x + 1);

        i64 consecutiveZerosOnRight = custom::countr_zero(tempBitmap & mask);

        // TODO: fix constant
        if (consecutiveZerosOnRight == 64) {
            return std::make_pair(-1, nullptr);
        }
        return std::make_pair(consecutiveZerosOnRight, this->vals[consecutiveZerosOnRight]);
    }

    // helper for predecessor to get the value associated with the max field of the corresponding cluster, if that's the answer
    // this function has some important assumptions, and should not be used in any other context
    V getValue(i64 x) {
        // important assumption: x definitely exists here
        if (this->min == this->max) {
            return this->minVal;
        }
        return this->vals[x];
    }

    std::pair<i64, V> predecessor(i64 x) {
        // if the leaf is empty, return -1
        // TODO: this is never reached if I appropriately null-check in the parent
        if (this->min == -1) return std::make_pair(-1, nullptr);

        // arriving here means that this->max IS NOT -1, because there's at least one element in the leaf
        // if the query is larger than the maximum, return the maximum
        if (x > this->max) {
            if (this->min == this->max) {
                return std::make_pair(this->max, this->minVal);
            }
            return std::make_pair(this->max, this->vals[this->max]);
        }

        if (x <= this->min) {
            return std::make_pair(-1, nullptr);
        }

        // if there's only one key in this leaf, and the query is not larger than it (checked in the if statement above), return -1
        if (this->min == this->max) {
            return std::make_pair(-1, nullptr);
        }

        // if we arrive here, there is at least 2 elements in this leaf
        // the query is <= this->max, and the max IS set in the bitmap
        // the final result depends on two cases:
        // 1. there is 2 elements => the result will be the min, which is not in the bitmap
        // 2. there is 3 or more elements => the result will be some bit set somewhere
        ui64 tempBitmap = this->bitmap;
        ui64 mask = ~(0xFFFFFFFFFFFFFFFFu << (x));

        int consecutiveZerosOnLeft = custom::countl_zero(tempBitmap & mask);
        // TODO: fix the constant here
        int firstSetBitBeforeMe = 64 - consecutiveZerosOnLeft - 1;
        if (firstSetBitBeforeMe == -1) {
            return std::make_pair(this->min, this->minVal);
        }
        return std::make_pair(firstSetBitBeforeMe, this->vals[firstSetBitBeforeMe]);
    }

    void insertToEmptyVEB(i64 x, V val) {
        this->min = x;
        this->max = x;
        this->minVal = val;
    }

    // upsert updates the value if the key exists
    bool upsert(i64 x, V val) {
        // IT'S A BIT DIFFERENT IN UPSERT
        // if (x == this->min || x == this->max) {
        //     return false;
        // }

        if (x == this->min) {
            // update the value
            this->minVal = val;
            // but return false because we want a correct checksum
            return false;
        }

        if (x == this->max) {
            // update the value
            // the value IS in the array, because there is at least 2 elems in this leaf
            this->vals[x] = val;
            // but return false, because we want a correct checksum
            return false;
        }

        // easy case: tree is empty
        if (this->min == -1) {
            this->insertToEmptyVEB(x, val);
            return true;
        }

        bool inserted = false;

        if (x < this->min) {
            // neeed to insert the old min into the bit vector (the minimum is never stored in the bit vector to be compliant with veb requirements)
            // inserted = true; NO NEED TO THIS, I'M ALREADY DOING IT LATER
            i64 temp = x;
            x = this->min;
            this->min = temp;

            // i can update the value because i know the key doesn't exist (insert vs upsert)
            V tempVal = this->minVal;
            this->minVal = val;
            val = tempVal;
        }

        // now insert the key
        inserted = ((this->bitmap) & (1ULL << x)) == 0;

        if (inserted) {
            this->bitmap = this->bitmap | (1ULL << x);
        }

        // update the value either way, because this is how upsert works
        this->vals[x] = val;

        if (x > this->max) {
            this->max = x;
        }

        // you might return false, but you have updated the corresponding value, which is expected from upsert
        return inserted;
    }

    // insert doesn't update the value if the key exists
    bool insert(i64 x, V val) {
        // AVOID RE-INSERTING THE MINIMUM OR THE MAXIMUM INSIDE THE CLUSTER
        if (x == this->min || x == this->max) {
            return false;
        }

        // easy case: tree is empty
        if (this->min == -1) {
            this->insertToEmptyVEB(x, val);
            return true;
        }

        bool inserted = false;

        if (x < this->min) {
            // neeed to insert the old min into the bit vector (the minimum is never stored in the bit vector to be compliant with veb requirements)
            // inserted = true; NO NEED TO THIS, I'M ALREADY DOING IT LATER
            i64 temp = x;
            x = this->min;
            this->min = temp;

            // i can update the value because i know the key doesn't exist (insert vs upsert)
            V tempVal = this->minVal;
            this->minVal = val;
            val = tempVal;
        }

        // now insert the key
        inserted = ((this->bitmap) & (1ULL << x)) == 0;

        if (inserted) {
            this->bitmap = this->bitmap | (1ULL << x);
            this->vals[x] = val;
        }

        if (x > this->max) {
            this->max = x;
        }

        return inserted;
    }

    bool del(i64 x) {
        // significantly improves throughput
        if (x > this->max || x < this->min) {
            return false;
        }

        // there's only 1 element in V
        // rare
        if (this->min == this->max) {
            if (this->min == x) {
                this->min = -1;
                this->max = -1;
                this->bitmap = 0ULL;
                this->minVal = (V) nullptr;
                return true;
            }
            return false;
        }

        bool erased = false;
        if (x == this->min) {
            // I'm deleting the min, so there must be a set of consecutive zeros on the LSB side of bitmap
            int consecutiveZerosOnRight = custom::countr_zero(this->bitmap);

            this->min = consecutiveZerosOnRight;
            this->minVal = this->vals[this->min];
            this->vals[this->min] = (V) nullptr;

            // now delete the new min from the bits
            ui64 mask = ~(1ULL << consecutiveZerosOnRight);
            this->bitmap = this->bitmap & mask;
            return true;
        }

        // if it's not the min, we can just unset the bit
        ui64 mask = (1ULL << x);
        if (!(this->bitmap & mask)) {
            // doesn't exist...
            return false;
        }

        // it exists, and we can delete it
        this->bitmap = this->bitmap & ~(mask);
        this->vals[x] = (V) nullptr;

        if (x == this->max) {
            int consecutiveZerosOnLeft = custom::countl_zero(this->bitmap);
            // TODO: fix the constant here
            int newMax = 64 - consecutiveZerosOnLeft - 1;
            this->max = newMax == -1 ? this->min : newMax;
        }
        return true;
    }

};

template <typename V>
class VebKVInternal {
public:
    i64 u;
    VebKVInternal **clusters;
    VebKInternal *summary;
    V minVal;
    volatile i64 min;
    volatile i64 max;

    VebKVInternal(i64 u) {
        this->u = u;
        this->min = -1;
        this->max = -1;
        this->summary = nullptr;
        // newing an array of nClusters pointers to ChoppedOptim objects
        UniverseInfo ui;

        if (kvMap.find(u) != kvMap.end()) {
            ui = kvMap[u];
        } else {
            ui = divide_node(u, CUTOFF_POWER, true);
        }

        this->clusters = new VebKVInternal *[ui.nClusters];
        // for (i64 i = 0; i < ui.nClusters; ++i) {
        //     this->clusters[i] = nullptr;
        // }
        std::fill_n(this->clusters, ui.nClusters, nullptr);
    }

    void makeMeReusable() {
        this->min = -1;
        this->max = -1;
        this->minVal = (V) nullptr;
        // this->summary = nullptr;
        // UniverseInfo ui = kvMap[this->u];
        // for (i64 i = 0; i < ui.nClusters; ++i) {
        //     this->clusters[i] = nullptr;
        // }
    }

    ~VebKVInternal() {
        for (ui64 i = 0; i < kvMap[this->u].nClusters; ++i) {
            if (this->clusters[i] != nullptr) {
                if (IS_LEAF(this->clusters[i])) {
                    auto cluster = PTR_TO_KVLEAF(this->clusters[i]);
                    delete cluster;
                } else {
                    auto cluster = PTR_TO_KVINTERNAL(this->clusters[i]);
                    delete cluster;
                }
            }
        }
        if (this->summary != nullptr) {
            if (IS_LEAF(this->summary)) {
                auto summary = PTR_TO_KVLEAF(this->summary);
                delete summary;
            } else {
                auto summary = PTR_TO_KVINTERNAL(this->summary);
                delete summary;
            }
        }
        delete[] this->clusters;
    }

    void populate_maps(i64 _u, bool isKV) {
        // printf("handling %ld as %s\n", _u, (isKV ? "KV" : "K"));
        UniverseInfo ui = divide_node(_u, CUTOFF_POWER, isKV);

        if (isKV) {
            if (kvMap.find(_u) == kvMap.end()) {
                // printf("Inserting kvMap[%ld]={lowBits: %ld, highBits: %ld}\n", _u, ui.lowBits, ui.highBits);
                kvMap.insert(pair<i64, UniverseInfo>(_u, ui));
            }

        } else {
            if (kMap.find(_u) == kMap.end()) {
                // printf("Inserting kMap[%ld]={lowBits: %ld, highBits: %ld}\n", _u, ui.lowBits, ui.highBits);
                kMap.insert(pair<i64, UniverseInfo>(_u, ui));
            }
        }

        if (_u > CUTOFF) {
            populate_maps(ui.nClusters, false);
            populate_maps(ui.clusterSize, isKV);
        }
    }

    void initThread(const int tid) {
        kvMap.clear();
        kMap.clear();
        this->populate_maps(this->u, true);

        for (const auto &pair : kMap) {
            i64 _u = pair.first;
            UniverseInfo _ui = pair.second;
            // printf("kMap[%ld]={lowBits: %ld, highBits: %ld}\n", _u, _ui.lowBits, _ui.highBits);
            if (_u > CUTOFF) {
                for (int i = 0; i < POINTERS_PER_POOL; ++i) {
                    kPool[_u].push_back((void *)new VebKInternal(_u));
                }
            } else {
                for (int i = 0; i < POINTERS_PER_POOL; ++i) {
                    kPool[_u].push_back((void *)new VebKLeaf(_u));
                }
            }
        }

        for (const auto &pair : kvMap) {
            i64 _u = pair.first;
            UniverseInfo _ui = pair.second;

            // printf("kvMap[%ld]={lowBits: %ld, highBits: %ld}\n", _u, _ui.lowBits, _ui.highBits);

            // skip the root node, no kvPool required there
            if (_u == this->u) continue;

            if (_u > CUTOFF) {
                for (int i = 0; i < POINTERS_PER_POOL; ++i) {
                    kvPool[_u].push_back((void *)new VebKVInternal<V>(_u));
                }
            } else {
                for (int i = 0; i < POINTERS_PER_POOL; ++i) {
                    kvPool[_u].push_back((void *)new VebKVLeaf<V>(_u));
                }
            }
        }
    }

    void deinitThread() {
    }

    bool member(i64 x) {
        if (x == this->min || x == this->max) {
            return true;
        }

        // if (this->u <= CUTOFF) {
        //     ui64 curBits = (ui64)this->clusters;
        //     return curBits & (1ULL << x);
        // }

        // if (this->u == 2) {
        //     // if it's neither min nor max, and we can't recurse any further,
        //     we're done return false;
        // }
        UniverseInfo ui = kvMap[this->u];

        i64 h = HIGH(x, ui);
        if (this->clusters[h] == nullptr) return false;

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
            return cluster->member(LOW(x, ui));
        } else {
            auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
            return cluster->member(LOW(x, ui));
        }

        // return this->clusters[h]->member(LOW(x, ui));
    }

    std::pair<i64, V> successor(i64 x) {
        // the query is smaller than the minimum => return the minimum
        if (this->min != -1 && x < this->min) {
            return std::make_pair(this->min, this->minVal);
        }
        UniverseInfo ui = kvMap[this->u];
        i64 h = HIGH(x, ui);

        if (this->clusters[h] != nullptr) {
            // the corresponding cluster exists... the successor "might" be there
            i64 l = LOW(x, ui);

            if (IS_LEAF(this->clusters[h])) {
                auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
                i64 maxLow = cluster->max;

                // TODO: do you have to check -1?
                if (maxLow != -1 && l < maxLow) {
                    auto succPair = cluster->successor(l);
                    i64 offset = succPair.first;
                    succPair.first = INDEX(h, offset, ui);
                    return succPair;
                }

            } else {
                auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
                i64 maxLow = cluster->max;

                // TODO: do you have to check -1?
                if (maxLow != -1 && l < maxLow) {
                    auto succPair = cluster->successor(l);
                    i64 offset = succPair.first;
                    succPair.first = INDEX(h, offset, ui);
                    return succPair;
                }
            }
        }

        // EITHER the corresponding cluster does not exist
        // OR it exists BUT the successor is not in there, so gotta look at other clusters (see the successor of h in the summary)

        // if there is no summary, there is no successor
        if (this->summary == nullptr) {
            return std::make_pair(-1, nullptr);
        }

        if (IS_LEAF(this->summary)) {
            auto summary = PTR_TO_KLEAF(this->summary);
            i64 succCluster = summary->successor(h);
            // nothing found in summary
            // => successor does not exist
            if (succCluster == -1) {
                return std::make_pair(-1, nullptr);
            }

            if (IS_LEAF(this->clusters[succCluster])) {
                // TODO: is this if statement necessary?
                // summary is leaf, so the cluster MUST be a leaf
                auto cluster = PTR_TO_KVLEAF(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return std::make_pair(INDEX(succCluster, offset, ui), cluster->minVal);
            } else {
                auto cluster = PTR_TO_KVINTERNAL(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return std::make_pair(INDEX(succCluster, offset, ui), cluster->minVal);
            }

        } else {
            auto summary = PTR_TO_KINTERNAL(this->summary);
            i64 succCluster = summary->successor(h);
            // nothing found in summary
            // => successor does not exist
            if (succCluster == -1) {
                return std::make_pair(-1, nullptr);
            }

            if (IS_LEAF(this->clusters[succCluster])) {
                auto cluster = PTR_TO_KVLEAF(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return std::make_pair(INDEX(succCluster, offset, ui), cluster->minVal);
            } else {
                auto cluster = PTR_TO_KVINTERNAL(this->clusters[succCluster]);
                i64 offset = cluster->min;
                return std::make_pair(INDEX(succCluster, offset, ui), cluster->minVal);
            }
        }
    }

    // helper for predecessor to get the value associated with the max field of the corresponding cluster, if that's the answer
    // this function has some important assumptions, and should not be used in any other context
    V getValue(i64 x) {
        if (x == this->min) return this->minVal;

        UniverseInfo ui = kvMap[this->u];
        i64 h = HIGH(x, ui);
        i64 l = LOW(x, ui);

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
            return cluster->getValue(l);
        } else {
            auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
            return cluster->getValue(l);
        }
    }

    std::pair<i64, V> predecessor(i64 x) {
        // the query is bigger than the maximim => return the maximum

        // IMPORTANT: THIS IS A NICE LITTLE OPTIMIZATION, BUT SINCE I DON'T HAVE ACCESS TO THE
        // VALUE ASSOCIATED WITH this->max here (this is not a leaf node), MAYBE I CAN COMMENT IT OUT
        // SO THAT I TRAVERSE FURTHER AND FIND THE PREDECESSOR WHERE IT IS ACTUALLY STORED
        //
        // if (this->max != -1 && x > this->max) {
        //     return this->max;
        // }

        if (this->min == -1) {
            // empty node. it is RARELY called => ONLY on an empty root. We don't want to fail then
            // otherwise, this entire if statement can be omitted
            return std::make_pair(-1, nullptr);
        }

        if (x > this->max) {
            if (this->max == this->min) {
                // in this case, I can return the value safely and quickly because it's stored in minVal
                return std::make_pair(this->max, this->minVal);
            } else {
                // in this case, I'll do nothing because I want to go further to find the actual node where the key is stored
            }
        }

        UniverseInfo ui = kvMap[this->u];
        i64 h = HIGH(x, ui);

        if (this->clusters[h] != nullptr) {
            // the corresponding cluster exists... the predecessor "might" be there
            i64 l = LOW(x, ui);

            if (IS_LEAF(this->clusters[h])) {
                auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
                i64 minLow = cluster->min;

                if (minLow != -1 && minLow < l) {
                    auto predPair = cluster->predecessor(l);
                    i64 offset = predPair.first;
                    predPair.first = INDEX(h, offset, ui);
                    return predPair;
                }

            } else {
                auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
                i64 minLow = cluster->min;

                if (minLow != -1 && minLow < l) {
                    auto predPair = cluster->predecessor(l);
                    i64 offset = predPair.first;
                    predPair.first = INDEX(h, offset, ui);
                    return predPair;
                }
            }
        }

        // EITHER the corresponding cluster does not exist
        // OR it exists BUT the predecessor is not in there, so gotta look at other clusters (see the predecessor of h in the summary)

        // if there is no summary, there is no predecessor
        if (this->summary == nullptr) {
            return std::make_pair(-1, nullptr);
        }

        if (IS_LEAF(this->summary)) {
            auto summary = PTR_TO_KLEAF(this->summary);
            i64 predCluster = summary->predecessor(h);

            // IMPORTANT: different from sucecssor
            if (predCluster == -1) {
                // predecessor might be stored in a min field somewhere
                // and we didn't see it because min is not stored in any clusters
                if (this->min != -1 && x > this->min) {
                    return std::make_pair(this->min, this->minVal);
                }
                return std::make_pair(-1, nullptr);
            }

            if (IS_LEAF(this->clusters[predCluster])) {
                auto cluster = PTR_TO_KVLEAF(this->clusters[predCluster]);
                i64 answer = cluster->max;
                V answerVal = cluster->getValue(answer);
                ;
                return std::make_pair(INDEX(predCluster, answer, ui), answerVal);
            } else {
                auto cluster = PTR_TO_KVINTERNAL(this->clusters[predCluster]);
                i64 answer = cluster->max;

                V answerVal = cluster->getValue(answer);
                ;
                return std::make_pair(INDEX(predCluster, answer, ui), answerVal);
                // return INDEX(predCluster, offset, ui);
            }
        } else {
            auto summary = PTR_TO_KINTERNAL(this->summary);
            i64 predCluster = summary->predecessor(h);

            // IMPORTANT: different from sucecssor
            if (predCluster == -1) {
                // predecessor might be stored in a min field somewhere
                // and we didn't see it because min is not stored in any clusters
                if (this->min != -1 && x > this->min) {
                    return std::make_pair(this->min, this->minVal);
                }
                return std::make_pair(-1, nullptr);
            }

            if (IS_LEAF(this->clusters[predCluster])) {
                auto cluster = PTR_TO_KVLEAF(this->clusters[predCluster]);
                i64 answer = cluster->max;
                V answerVal = cluster->getValue(answer);
                ;
                return std::make_pair(INDEX(predCluster, answer, ui), answerVal);

            } else {
                auto cluster = PTR_TO_KVINTERNAL(this->clusters[predCluster]);
                i64 answer = cluster->max;
                V answerVal = cluster->getValue(answer);
                return std::make_pair(INDEX(predCluster, answer, ui), answerVal);
            }
        }
    }

    void allocateClusterIfNeeded(i64 h) {
        if (this->clusters[h] == nullptr) {
            i64 clusterU = kvMap[this->u].clusterSize;
            void *newCluster = kvPool[clusterU].back();
            if (clusterU <= CUTOFF) {
                this->clusters[h] = MARK_PTR_AS_KVLEAF(newCluster);
            } else {
                this->clusters[h] = (VebKVInternal *)newCluster;
            }
            kvPool[clusterU].pop_back();
            kvToRefill.push_back(clusterU);
        }
    }

    void allocateSummaryIfNeeded() {
        if (this->summary == nullptr) {
            i64 summaryU = kvMap[this->u].nClusters;
            void *newSummary = kPool[summaryU].back();
            if (summaryU <= CUTOFF) {
                this->summary = MARK_PTR_AS_KLEAF(newSummary);
            } else {
                this->summary = (VebKInternal *)newSummary;
            }
            kPool[summaryU].pop_back();
            kToRefill.push_back(summaryU);
        }
    }

    void insertToEmptyVEB(i64 x, V val) {
        this->min = x;
        this->max = x;
        this->minVal = val;
    }

    bool upsert(i64 x, V val) {
        if (x == this->min) {
            // upsert, update the value
            this->minVal = val;
            // but return false, because we want a correct checksum
            return false;
        }

        // if x == this->max, do nothing, because you need to go down to the corresponding cluster and update the value

        // easy case: tree is empty
        if (this->min == -1) {
            this->insertToEmptyVEB(x, val);
            return true;
        }

        bool inserted;
        // x is our new minimum
        // set x as the new min, then insert the old min into the tree
        if (x < this->min) {
            // if it's less than the minimum, it doesn't exist in the tree
            // what about lower levels?
            // if it's smaller than the minimum in level l, it can't be
            // greater than any of the minumums on level l + k because the
            // top level minimum is the absolute minimum
            inserted = true;

            i64 temp = x;
            x = this->min;
            this->min = temp;

            // set the value safely, because the key you inserted did not previously exist (insert vs upsert)
            V tempVal = this->minVal;
            this->minVal = val;
            val = tempVal;
        }

        UniverseInfo ui = kvMap[this->u];
        i64 h = HIGH(x, ui);
        i64 l = LOW(x, ui);

        this->allocateClusterIfNeeded(h);

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
            if (cluster->min == -1) {
                this->allocateSummaryIfNeeded();
                // call KCALL because summary is keyonly

                KCALL_VARIADIC(this->summary, insert, h);
                cluster->insertToEmptyVEB(l, val);
                inserted = true;
            } else {
                inserted = cluster->upsert(l, val);
            }

        } else {
            auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
            if (cluster->min == -1) {
                this->allocateSummaryIfNeeded();
                // call KCALL because summary is keyonly
                KCALL_VARIADIC(this->summary, insert, h);
                cluster->insertToEmptyVEB(l, val);
                inserted = true;
            } else {
                inserted = cluster->upsert(l, val);
            }
        }

        if (x > this->max) {
            this->max = x;
        }

        return inserted;
    }

    bool insert(i64 x, V val) {
        // AVOID RE-INSERTING THE MINIMUM OR THE MAXIMUM INSIDE THE CLUSTER
        if (x == this->min || x == this->max) {
            return false;
        }

        // easy case: tree is empty
        if (this->min == -1) {
            this->insertToEmptyVEB(x, val);
            return true;
        }

        bool inserted;
        // x is our new minimum
        // set x as the new min, then insert the old min into the tree
        if (x < this->min) {
            // if it's less than the minimum, it doesn't exist in the tree
            // what about lower levels?
            // if it's smaller than the minimum in level l, it can't be
            // greater than any of the minumums on level l + k because the
            // top level minimum is the absolute minimum
            inserted = true;

            i64 temp = x;
            x = this->min;
            this->min = temp;

            V tempVal = this->minVal;
            this->minVal = val;
            val = tempVal;
        }

        UniverseInfo ui = kvMap[this->u];
        i64 h = HIGH(x, ui);
        i64 l = LOW(x, ui);

        this->allocateClusterIfNeeded(h);

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
            if (cluster->min == -1) {
                this->allocateSummaryIfNeeded();
                // call KCALL because summary is keyonly

                KCALL_VARIADIC(this->summary, insert, h);
                cluster->insertToEmptyVEB(l, val);
                inserted = true;
            } else {
                inserted = cluster->insert(l, val);
            }

        } else {
            auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
            if (cluster->min == -1) {
                this->allocateSummaryIfNeeded();
                // call KCALL because summary is keyonly
                KCALL_VARIADIC(this->summary, insert, h);
                cluster->insertToEmptyVEB(l, val);
                inserted = true;
            } else {
                inserted = cluster->insert(l, val);
            }
        }

        if (x > this->max) {
            this->max = x;
        }

        return inserted;
    }

    // this is supposed to not assume x already exists in V
    bool del(i64 x) {
        // significantly improves throughput
        if (x > this->max || x < this->min) {
            return false;
        }
        // there's only 1 element in V
        // rare
        if (this->min == this->max) {
            if (this->min == x) {
                this->min = -1;
                this->max = -1;
                this->minVal = (V) nullptr;
                return true;
            }
            return false;
        }

        // from now on, V has at least 2 elements
        // if (this->u == 2) {
        //     // we know that V has 2 elems
        //     // we're at the base case
        //     // one of them must be x
        //     // delete it and set min and max accordingly
        //     this->min = 1 - x;
        //     this->max = this->min;

        //     return true;
        // }

        bool erased;

        // from now on, V has at least 2 elements and u >= 4
        UniverseInfo ui = kvMap[this->u];

        // if deleting the min value,
        // set one of the elems as the new min
        // delete that element from inside the cluster
        if (x == this->min) {
            // no need to do nullptr check on summary because if there are
            // at least 2 elements in the structure, the summary has been
            // allocated

            if (IS_LEAF(this->summary)) {
                auto summary = PTR_TO_KLEAF(this->summary);
                i64 firstCluster = summary->min;

                // OLD COMMENTS, NOT APPLICABLE NOW:
                // summary is leaf, so cluster MUST be a leaf too because summarySize >= clusterSize always
                // NEW COMMENTS:
                // No assumptions!
                if (IS_LEAF(this->clusters[firstCluster])) {
                    auto cluster = PTR_TO_KVLEAF(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;
                    this->minVal = cluster->minVal;
                } else {
                    auto cluster = PTR_TO_KVINTERNAL(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;
                    this->minVal = cluster->minVal;
                }
            } else {
                auto summary = PTR_TO_KINTERNAL(this->summary);
                i64 firstCluster = summary->min;
                // summary is not a leaf, so cluster could be anything
                if (IS_LEAF(this->clusters[firstCluster])) {
                    auto cluster = PTR_TO_KVLEAF(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;
                    this->minVal = cluster->minVal;
                } else {
                    auto cluster = PTR_TO_KVINTERNAL(this->clusters[firstCluster]);
                    x = INDEX(firstCluster, cluster->min, ui);
                    this->min = x;
                    this->minVal = cluster->minVal;
                }
            }
        }

        i64 h = HIGH(x, ui);
        //  this if statement only, handles the case for lazy veb
        // (prevents from seg faults)
        // reclamation remaining though
        if (this->clusters[h] == nullptr) {
            return false;
        }

        i64 l = LOW(x, ui);

        if (IS_LEAF(this->clusters[h])) {
            auto cluster = PTR_TO_KVLEAF(this->clusters[h]);
            erased = cluster->del(l);
            // if successfully deleted x and the cluster is empty now
            if (cluster->min == -1) {
                // free the memory
                // IMPORTANT: push back the marked pointer, not the one you unmarked to read the min of!
                kvToReclaim.push_back(this->clusters[h]);
                this->clusters[h] = nullptr;

                // update summary so that it reflects the emptiness
                if (IS_LEAF(this->summary)) {
                    auto summary = PTR_TO_KLEAF(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KVLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the marked pointer, not the one you unmarked to read the min of!
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                } else {
                    auto summary = PTR_TO_KINTERNAL(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KVLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the unmarked pointer (even though it wasn't marked to begin with, it is good practice to avoid segfaults)
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                }
            } else if (x == this->max) {
                this->max = INDEX(h, cluster->max, ui);
            }

        } else {
            auto cluster = PTR_TO_KVINTERNAL(this->clusters[h]);
            erased = cluster->del(l);

            // OLD COMMENTS, NOT APPLICABLE NOW
            // cluster is internal, so the summary must be internal too
            // NEW COMMENTS: no assumptions anymore

            if (cluster->min == -1) {
                // IMPORTANT: push back the unmarked pointer (even though it wasn't marked to begin with, it is good practice to avoid segfaults)
                kvToReclaim.push_back(this->clusters[h]);
                this->clusters[h] = nullptr;

                if (IS_LEAF(this->summary)) {
                    auto summary = PTR_TO_KLEAF(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the marked pointer, not the one you unmarked to read the min of!
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                } else {
                    auto summary = PTR_TO_KINTERNAL(this->summary);
                    summary->del(h);
                    // if we deleted the max element, we need to find the new max
                    if (x == this->max) {
                        i64 summaryMax = summary->max;
                        // only 1 elem remaining
                        if (summaryMax == -1) {
                            this->max = this->min;
                        } else {
                            auto summaryMaxCluster = PTR_TO_KLEAF(this->clusters[summaryMax]);
                            this->max =
                                INDEX(summaryMax, summaryMaxCluster->max,
                                      ui);
                        }
                    }
                    if (summary->min == -1) {
                        // IMPORTANT: push back the unmarked pointer (even though it wasn't marked to begin with, it is good practice to avoid segfaults)
                        kToReclaim.push_back(this->summary);
                        this->summary = nullptr;
                    }
                }
            } else if (x == this->max) {
                this->max = INDEX(h, cluster->max, ui);
            }
        }
        return erased;
    }

    i64 getSumOfKeys() {
        i64 res = 0;
        for (i64 i = 0; i < this->u; ++i) {
            if (this->member(i)) {
                res += i;
            }
        }
        return res;
    }

    bool memberDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(SEARCH, member, x);
        return retval;
    }

    bool upsertDriver(const int tid, i64 x, V val) {
        bool retval = false;
        TLE(INSERT, upsert, x COMMA val);
        if (retval) {
            for (auto it = kvToRefill.begin(); it != kvToRefill.end(); ++it) {
                i64 neededSize = *it;
                if (neededSize > CUTOFF) {
                    while (kvPool[neededSize].size() < POINTERS_PER_POOL) {
                        kvPool[neededSize].push_back((void *)new VebKVInternal<V>(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_kv, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                } else {
                    while (kvPool[neededSize].size() < POINTERS_PER_POOL) {
                        kvPool[neededSize].push_back((void *)new VebKVLeaf<V>(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_kv, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                }
            }

            kvToRefill.clear();

            for (auto it = kToRefill.begin(); it != kToRefill.end(); ++it) {
                i64 neededSize = *it;
                if (neededSize > CUTOFF) {
                    while (kPool[neededSize].size() < POINTERS_PER_POOL) {
                        kPool[neededSize].push_back((void *)new VebKInternal(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_k, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                } else {
                    while (kPool[neededSize].size() < POINTERS_PER_POOL) {
                        kPool[neededSize].push_back((void *)new VebKLeaf(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_k, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                }
            }

            kToRefill.clear();
        }
        return retval;
    }

    bool insertIfAbsentDriver(const int tid, i64 x, V val) {
        bool retval = false;
        TLE(INSERT, insert, x COMMA val);
        if (retval) {
            for (auto it = kvToRefill.begin(); it != kvToRefill.end(); ++it) {
                i64 neededSize = *it;
                if (neededSize > CUTOFF) {
                    while (kvPool[neededSize].size() < POINTERS_PER_POOL) {
                        kvPool[neededSize].push_back((void *)new VebKVInternal<V>(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_kv, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                } else {
                    while (kvPool[neededSize].size() < POINTERS_PER_POOL) {
                        kvPool[neededSize].push_back((void *)new VebKVLeaf<V>(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_kv, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                }
            }

            kvToRefill.clear();

            for (auto it = kToRefill.begin(); it != kToRefill.end(); ++it) {
                i64 neededSize = *it;
                if (neededSize > CUTOFF) {
                    while (kPool[neededSize].size() < POINTERS_PER_POOL) {
                        kPool[neededSize].push_back((void *)new VebKInternal(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_k, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                } else {
                    while (kPool[neededSize].size() < POINTERS_PER_POOL) {
                        kPool[neededSize].push_back((void *)new VebKLeaf(neededSize));
                        // GSTATS_ADD_IX(tid, num_pool_alloc_k, 1, (int)ceil(log2(log2((double)neededSize))));
                    }
                }
            }

            kToRefill.clear();
        }
        return retval;
    }



    bool delDriver(const int tid, i64 x) {
        bool retval = false;

        TLE(DELETE, del, x);

        if (retval) {
            for (auto it = kvToReclaim.begin(); it != kvToReclaim.end(); ++it) {
                // VebKInternal *temp = (VebKInternal *)*it;
                // temp->makeMeReusable();
                // kvPool[temp->u].push_back((void *)temp);
                void *temp = *it;
                if (IS_LEAF(temp)) {
                    auto node = PTR_TO_KVLEAF(temp);
                    if (kvPool[node->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
                        node->makeMeReusable();
                        kvPool[node->u].push_back(temp);
                        // GSTATS_ADD_IX(tid, num_pool_free_kv, 1, (int)ceil(log2(log2((double)node->u))));
                    } else {
                        delete node;
                    }

                } else {
                    auto node = PTR_TO_KVINTERNAL(temp);
                    if (kvPool[node->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
                        node->makeMeReusable();
                        kvPool[node->u].push_back(temp);
                        // GSTATS_ADD_IX(tid, num_pool_free_kv, 1, (int)ceil(log2(log2((double)node->u))));
                    } else {
                        delete node;
                    }
                }
            }
            kvToReclaim.clear();

            for (auto it = kToReclaim.begin(); it != kToReclaim.end(); ++it) {
                void *temp = *it;
                if (IS_LEAF(temp)) {
                    auto node = PTR_TO_KLEAF(temp);
                    if (kPool[node->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
                        node->makeMeReusable();
                        kPool[node->u].push_back(temp);
                        // GSTATS_ADD_IX(tid, num_pool_free_k, 1, (int)ceil(log2(log2((double)node->u))));
                    } else {
                        delete node;
                    }

                } else {
                    auto node = PTR_TO_KINTERNAL(temp);
                    if (kPool[node->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
                        node->makeMeReusable();
                        kPool[node->u].push_back(temp);
                        // GSTATS_ADD_IX(tid, num_pool_free_k, 1, (int)ceil(log2(log2((double)node->u))));
                    } else {
                        delete node;
                    }
                }
            }
            kToReclaim.clear();
        }
        return retval;
    }

    std::pair<i64, V> successorDriver(const int tid, i64 x) {
        std::pair<i64, V> retval;
        TLE(SUCCESSOR, successor, x);
        return retval;
    }

    std::pair<i64, V> predecessorDriver(const int tid, i64 x) {
        std::pair<i64, V> retval;
        TLE(PREDECESSOR, predecessor, x);
        return retval;
    }
};

#endif