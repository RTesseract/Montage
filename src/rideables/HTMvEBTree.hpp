#ifndef HTM_VEB_TREE_P
#define HTM_VEB_TREE_P

#include <cmath>
#include <cstdint>
#include <map>
#include <plaf.h>

#include "RSet.hpp"

#include <immintrin.h>
#include "GlobalLock.hpp"

#define MAX_RETRIES 35
#define PAUSE_COUNT 2

#define PAUSE()                                      \
    for (int __pc = 0; __pc < PAUSE_COUNT; ++__pc) { \
        _mm_pause();                                 \
    }

#if 1
#define TLE(op, func, args)                    \
    int retriesLeft = MAX_RETRIES;             \
    int txnStatus;                             \
    retry:                                     \
    txnStatus = _xbegin();                     \
    if (txnStatus == _XBEGIN_STARTED) {        \
        if (readLock(&globalLock)) _xabort(0); \
        retval = this->func(args);             \
        _xend();                               \
    } else {                                   \
        while (readLock(&globalLock)) {        \
            PAUSE();                           \
        }                                      \
        if (--retriesLeft > 0) goto retry;     \
        acquireLock(&globalLock);              \
        retval = this->func(args);             \
        releaseLock(&globalLock);              \
    }

#else
#define TLE(op, func, args)                    \
        acquireLock(&globalLock);              \
        retval = this->func(args);             \
        releaseLock(&globalLock);              \

#endif

#define CUTOFF 2
#define CUTOFF_POWER 0

#define LOW(x, ui) ((x) & (ui.lowMask))
#define HIGH(x, ui) ((x) >> (ui.lowBits))
#define INDEX(cluster, offset, ui) ((cluster) * (ui.clusterSize) + (offset))

typedef int64_t i64;
typedef uint64_t ui64;

typedef struct {
    i64 clusterSize;  // size of each cluster (lowerRoot)
    i64 nClusters;    // size of summary (nClusters) (upperRoot)
    i64 lowBits;      // (lowerPower)
    i64 highBits;     // (lowerRoot)
    i64 lowMask;      // clusterSize - 1
} UniverseInfo;

PAD;
volatile int globalLock = 0;
PAD;
thread_local map<i64, UniverseInfo> kMap;     // for key-only structures

template <class K>
class HTMvEBTree;

inline int HTMvEBTreeRange;

template <class K>
class HTMvEBTreeNode
{
public:
    i64 u;
    HTMvEBTreeNode **clusters;
    HTMvEBTreeNode *summary;
    // PAD;
    volatile i64 min;
    volatile i64 max;
    // PAD;
    // Constructor

    HTMvEBTreeNode(i64 u) {
        this->u = u;
        this->min = -1;
        this->max = -1;

        if (u == 2) {
        } else {

            UniverseInfo ui;

            if (kMap.find(u) != kMap.end()) {
                ui = kMap[u];
            } else {
                ui = HTMvEBTree<K>::divide_node(u, CUTOFF_POWER, false);
            }

            this->summary = new HTMvEBTreeNode(ui.nClusters);

            this->clusters = new HTMvEBTreeNode *[ui.nClusters];
            for (i64 i = 0; i < ui.nClusters; ++i) { 
                this->clusters[i] = new HTMvEBTreeNode(ui.clusterSize);
            }
        }
    }

    ~HTMvEBTreeNode() {
        if (this->u > 2) {
            for (ui64 i = 0; i < kMap[this->u].nClusters; ++i) {
                if (this->clusters[i] != nullptr) {
                    delete this->clusters[i];
                }
            }
            if (this->summary != nullptr) {
                delete this->summary;
            }
            delete[] this->clusters;
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
            // if it's smaller than the minimum in level l, it can't be greater than any of the minumums on level l + k
            // because the top level minimum is the absolute minimum
            inserted = true;

            i64 temp = x;
            x = this->min;
            this->min = temp;
        }

        if (this->u > 2) {
            UniverseInfo ui = kMap[this->u];
            i64 h = HIGH(x, ui);
            i64 l = LOW(x, ui);

            // this->allocateClusterIfNeeded(h);
            // the corresponding cluster is empty
            if (this->clusters[h]->min == -1) {
                // this->allocateSummaryIfNeeded();
                this->summary->insert(h);
                this->clusters[h]->insertToEmptyVEB(l);
                inserted = true;
            }

            // the corresponding cluster already has some elements
            else {
                inserted = this->clusters[h]->insert(l);
            }
        }

        if (x > this->max) {
            // if it's greater than the maximum, it must be a new element, and we must have inserted it.
            // NOT NECESSARY, ALREADY TAKEN CARE OF BEFORE
            // inserted = true;

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
            // else {
            // there's only 1 element, and it's not x
            return false;
            // }
        }

        // from now on, V has at least 2 elements
        if (this->u == 2) {
            // we know that V has 2 elems
            // we're at the base case
            // one of them must be x
            // delete it and set min and max accordingly
            this->min = 1 - x;
            this->max = this->min;

            return true;
        }

        // from now on, V has at least 2 elements and u >= 4
        UniverseInfo ui = kMap[this->u];

        // if deleting the min value,
        // set one of the elems as the new min
        // delete that element from inside the cluster
        if (x == this->min) {
            // no need to do nullptr check on summary because if there are at least 2 elements in the structure, the summary has been allocated
            i64 firstCluster = this->summary->min;
            x = INDEX(firstCluster, this->clusters[firstCluster]->min, ui);
            this->min = x;
        }

        i64 h = HIGH(x, ui);

        i64 l = LOW(x, ui);
        bool erased;
        // now delete x from the cluster
        erased = this->clusters[h]->del(l);

        // if successfully deleted x and the cluster is empty now
        if (this->clusters[h]->min == -1) {
            // free the memory
            // delete this->clusters[h];
            // kToReclaim.push_back(this->clusters[h]);
            // this->clusters[h] = nullptr;  // absence of this line causes segfaults

            // update summary so that it reflects the emptiness
            this->summary->del(h);

            // if we deleted the max element, we need to find the new max
            if (x == this->max) {
                i64 summaryMax = this->summary->max;

                // only 1 elem remaining
                if (summaryMax == -1) {
                    this->max = this->min;
                } else {
                    this->max = INDEX(summaryMax, this->clusters[summaryMax]->max, ui);
                }
            }

            // if (this->summary->min == -1) {
                // delete this->summary;
                // kToReclaim.push_back(this->summary);
                // this->summary = nullptr;  // absence of this line causes segfaults
            // }

        }

        // the cluster still has other elements after deleting x
        // we don't need to update the summary, but we might need to update the max
        // the erased check is not necessary
        // if it's equal to max, it's been deleted
        else if (x == this->max) {
            this->max = INDEX(h, this->clusters[h]->max, ui);
        }
        return erased;
    }

    bool insertDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(INSERT, insert, x);
        // if (retval) {
        //     for (auto it = kToRefill.begin(); it != kToRefill.end(); ++it) {
        //         i64 neededSize = *it;
        //         while (kPool[neededSize].size() < POINTERS_PER_POOL) {
        //             kPool[neededSize].push_back((void *)new HTMvEBTreeNode(neededSize));
        //         }
        //     }
        //     kToRefill.clear();
        // }
        return retval;
    }

    bool delDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(DELETE, del, x);

        // if (retval) {
        //     for (auto it = kToReclaim.begin(); it != kToReclaim.end(); ++it) {
        //         HTMvEBTreeNode *temp = (HTMvEBTreeNode *)*it;
        //         if (kPool[temp->u].size() < POINTERS_PER_POOL * POOL_GROW_COEFFICIENT) {
        //             temp->makeMeReusable();
        //             kPool[temp->u].push_back((void *)temp);
        //         } else {
        //             delete temp;
        //         }
        //     }
        //     kToReclaim.clear();
        // }
        return retval;
    }
};

template <class K>
class HTMvEBTree : public RSet<K>
{
private:
    void print_tree(HTMvEBTree<K> *node, bool summary, i64 level)
    {
        if (!node)
            return;
        UniverseInfo ui = kMap[node->u];
        printf("level %ld (%s): (u=%ld, min=%ld, max=%ld), (clusterSize=%ld, nClusters=%ld, lowBits=%ld, highBits=%ld, lowMask=%ld)\n", level, summary ? "summary": "cluster", node->u, node->min, node->max, ui.clusterSize, ui.nClusters, ui.lowBits, ui.highBits, ui.lowMask);
        // if (node->summary)
        //     print_tree(node->summary, true, level + 1);
        // if (node->clusters)
        //     for (i64 i = 0; i < ui.nClusters; ++i)
        //         print_tree(node->clusters[i], false, level + 1);
    }

    void print_tree(HTMvEBTree<K> *node)
    {
        printf("=====start=====\n");
        print_tree(node, false, 0);
        print_tree(node->clusters[0], false, 1);
        print_tree(node->clusters[0]->clusters[0], false, 2);
        print_tree(node->clusters[0]->clusters[1], false, 2);
        print_tree(node->clusters[1], false, 1);
        print_tree(node->clusters[1]->clusters[0], false, 2);
        print_tree(node->clusters[1]->clusters[1], false, 2);
        print_tree(node->clusters[2], false, 1);
        print_tree(node->clusters[2]->clusters[0], false, 2);
        print_tree(node->clusters[2]->clusters[1], false, 2);
        print_tree(node->clusters[3], false, 1);
        print_tree(node->clusters[3]->clusters[0], false, 2);
        print_tree(node->clusters[3]->clusters[1], false, 2);
        printf("=====end=====\n");
    }

public:
    HTMvEBTreeNode<K> *root;

    HTMvEBTree(): root(new HTMvEBTreeNode<K>(HTMvEBTreeRange)) {}

    static UniverseInfo divide_node(i64 u, int cutoffPower, bool isKV) {
        // first field is the number of clusters (size of summary)
        // second field of the size of each cluster
        UniverseInfo res;
        i64 lowerPower, lowerRoot, upperPower, upperRoot;

        double powers = log2(u);

        lowerPower = floor(powers / 2);
        upperPower = ceil(powers / 2);

    // #ifdef SHARD_24
    //     if (powers > 24) {
    //         lowerPower = 24;
    //         upperPower = powers - 24;
    //     } else if (powers < 24 && powers > 12) { 
    //         lowerPower = 12;
    //         upperPower = powers - 12;
    //     }
    // #endif

    // #ifdef FORCE_64_KV
    //     if (isKV) {
    //         while (lowerPower < cutoffPower && upperPower > 0) {
    //             lowerPower++;
    //             upperPower--;
    //         }
    //     }
    // #endif

    // #ifdef FORCE_64_K
    //     if (!isKV) {
    //         while (lowerPower < cutoffPower && upperPower > 0) {
    //             lowerPower++;
    //             upperPower--;
    //         }
    //     }
    // #endif

        lowerRoot = pow(2, lowerPower);  // size of each cluster
        upperRoot = pow(2, upperPower);  // nClusters, size of summary

        res.nClusters = upperRoot;
        res.highBits = upperPower;
        res.clusterSize = lowerRoot;
        res.lowBits = lowerPower;
        res.lowMask = lowerRoot - 1;

        return res;
    }

    void populate_maps(i64 _u, bool isKV) {
        // printf("handling %ld as %s\n", _u, (isKV ? "KV" : "K"));
        UniverseInfo ui = divide_node(_u, CUTOFF_POWER, isKV);

        if (kMap.find(_u) == kMap.end()) {
            kMap.insert(pair<i64, UniverseInfo>(_u, ui));
        }

        if (_u > CUTOFF) {
            populate_maps(ui.nClusters, false);
            populate_maps(ui.clusterSize, false);
        }
    }

    void initThread(const int tid) {
        kMap.clear();
        populate_maps(HTMvEBTreeRange, false);

        // for (const auto &pair : kMap) {
        //     i64 _u = pair.first;
        //     UniverseInfo _ui = pair.second;
        //     // printf("kMap[%ld]={lowBits: %ld, highBits: %ld, nClusters: %ld, clusterSize: %ld}\n", _u, _ui.lowBits, _ui.highBits, _ui.nClusters, _ui.clusterSize);
        //     for (int i = 0; i < POINTERS_PER_POOL; ++i) {
        //         kPool[_u].push_back((void *)new HTMvEBTreeNode(_u));
        //     }
        // }
    }

    // void deinitThread(const int tid) {
    // }

	bool get(K key, int tid) override { return true; }
	void put(K key, int tid) override {}
	bool insert(K key, int tid) override { return root->insertDriver(tid, key); }
	bool remove(K key, int tid) override { return root->delDriver(tid, key); }
};

template <class T> 
class HTMvEBTreeFactory : public RideableFactory
{
    Rideable* build(GlobalTestConfig* gtc)
    {
        return new HTMvEBTree<T>();
    }
};

#endif /* HTM_VEB_TREE_P */
