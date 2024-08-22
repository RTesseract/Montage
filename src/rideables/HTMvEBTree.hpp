#ifndef HTM_VEB_TREE_P
#define HTM_VEB_TREE_P

#include <cmath>
#include <cstdint>
#include <map>
#include <plaf.h>

#include "RSet.hpp"
#include "Recoverable.hpp"

#include <immintrin.h>
#include "GlobalLock.hpp"

// #define VEB_TEST
#if 0

#define MAX_RETRIES 35
#define PAUSE_COUNT 2

#define TLE(func, args)                                     \
    int retriesLeft = MAX_RETRIES;                          \
    unsigned int txnStatus;                                 \
retry:                                                      \
    txnStatus = _xbegin();                                  \
    if (txnStatus == _XBEGIN_STARTED) {                     \
        if (readLock(&globalLock)) _xabort(0);              \
        retval = this->func(args);                          \
        _xend();                                            \
    } else {                                                \
        while (readLock(&globalLock))                       \
            for (int __pc = 0; __pc < PAUSE_COUNT; ++__pc)  \
                _mm_pause();                                \
        if (--retriesLeft > 0) goto retry;                  \
        acquireLock(&globalLock);                           \
        retval = this->func(args);                          \
        releaseLock(&globalLock);                           \
    }
#else
#define TLE(func, args)         \
    acquireLock(&globalLock);   \
    retval = this->func(args);  \
    releaseLock(&globalLock);
#endif

#define CUTOFF 2
#define CUTOFF_POWER 0

#define LOW(x, ui) ((x) & (ui.lowMask))
#define HIGH(x, ui) ((x) >> (ui.lowBits))
#define INDEX(cluster, offset, ui) ((cluster) * (ui.clusterSize) + (offset))

typedef int64_t i64;
typedef uint64_t ui64;

struct UniverseInfo {
    i64 clusterSize;  // size of each cluster (lowerRoot)
    i64 nClusters;    // size of summary (nClusters) (upperRoot)
    i64 lowBits;      // (lowerPower)
    i64 highBits;     // (lowerRoot)
    i64 lowMask;      // clusterSize - 1
};

PAD;
volatile int globalLock = 0;
int HTMvEBTreeRange;
PAD;
thread_local map<i64, UniverseInfo> kMap;     // for key-only structures

UniverseInfo divide_node(i64 u, int cutoffPower, bool isKV) {
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

inline thread_local bool old_see_new;
inline thread_local deque<void *> pToRetire;

#ifdef VEB_TEST
inline char *bitmap;

inline char GET(i64 k) {
    const char mask = ((char)1) << (k % 8);
    const char val = bitmap[k / 8] & mask;
    return val ? 1 : 0;
}

inline void SET(i64 k) {
    bitmap[k / 8] = bitmap[k / 8] | (((char)1) << (k % 8));
}

inline void CLR(i64 k) {
    bitmap[k / 8] = bitmap[k / 8] & (~(((char)1) << (k % 8)));
}
#endif /* VEB_TEST */

#define COMMA ,

template <class K>
class HTMvEBTree : public RSet<K>, public Recoverable {
public:
    class Payload : public pds::PBlk {
        GENERATE_FIELD(K, key, Payload);
    public:
        Payload(K key) : m_key(key) {}
    };

    inline bool check_epoch(Payload *p) {
        return this->get_epoch() >= p->get_epoch();
    }

    class Node {
    public:
        i64 u;
        Node **clusters;
        Node *summary;
        HTMvEBTree *const ds;
        // PAD;
        volatile i64 min;
        volatile i64 max;
        // PAD;
        Payload *p1, *p2;

        Node(i64 u, HTMvEBTree *ds): u(u), clusters(nullptr), summary(nullptr), ds(ds), min(-1), max(-1), p1(nullptr), p2(nullptr) {
            if (u == 2) {
            } else {

                UniverseInfo ui;

                if (kMap.find(u) != kMap.end()) {
                    ui = kMap[u];
                } else {
                    ui = divide_node(u, CUTOFF_POWER, false);
                }

                this->summary = new Node(ui.nClusters, ds);

                this->clusters = new Node *[ui.nClusters];
                for (i64 i = 0; i < ui.nClusters; ++i) { 
                    this->clusters[i] = new Node(ui.clusterSize, ds);
                }
            }
        }

        ~Node() {
            if (this->u > 2) {
                for (i64 i = 0; i < kMap[this->u].nClusters; ++i) {
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

        void insertToEmptyVEB(i64 x, Payload *p) {
            this->min = x;
            this->max = x;
            p1 = p;
            p2 = p;
        }

        bool insert(i64 x, Payload *p) {
            // AVOID RE-INSERTING THE MINIMUM OR THE MAXIMUM INSIDE THE CLUSTER
            if (x == this->min || x == this->max) {
                return false;
            }

            // easy case: tree is empty
            if (this->min == -1) {
                this->insertToEmptyVEB(x, p);
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

                Payload *p_temp = p;
                p = p1;
                p1 = p_temp;
            }

            if (this->u > 2) {
                UniverseInfo ui = kMap[this->u];
                i64 h = HIGH(x, ui);
                i64 l = LOW(x, ui);

                // this->allocateClusterIfNeeded(h);
                // the corresponding cluster is empty
                if (this->clusters[h]->min == -1) {
                    // this->allocateSummaryIfNeeded();
                    this->summary->insert(h, p);
                    this->clusters[h]->insertToEmptyVEB(l, p);
                    inserted = true;
                }

                // the corresponding cluster already has some elements
                else {
                    inserted = this->clusters[h]->insert(l, p);
                }
            }

            if (x > this->max) {
                // if it's greater than the maximum, it must be a new element, and we must have inserted it.
                // NOT NECESSARY, ALREADY TAKEN CARE OF BEFORE
                // inserted = true;

                this->max = x;
                p2 = p;
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
                    if (!p1 || !p2) errexit("del: !p1 || !p2");
                    else if (p1 != p2) errexit("del: p1 != p2");
                    if (!ds->check_epoch(p1)) {
                        old_see_new = true;
                        return false;
                    }
                    this->min = -1;
                    this->max = -1;
                    pToRetire.push_back(p1);
                    p1 = nullptr;
                    p2 = nullptr;
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
                if (!p1 || !p2) errexit("del: !p1 || !p2 (#2)");
                else if (p1 == p2) errexit("del: p1 == p2");
                if (!x) {
                    if (!ds->check_epoch(p1)) {
                        old_see_new = true;
                        return false;
                    }
                    this->min = 1 - x;
                    this->max = this->min;
                    pToRetire.push_back(p1);
                    p1 = p2;
                } else {
                    if (!ds->check_epoch(p2)) {
                        old_see_new = true;
                        return false;
                    }
                    this->min = 1 - x;
                    this->max = this->min;
                    pToRetire.push_back(p2);
                    p2 = p1;
                }
                return true;
            }

            // from now on, V has at least 2 elements and u >= 4
            UniverseInfo ui = kMap[this->u];

            // if deleting the min value,
            // set one of the elems as the new min
            // delete that element from inside the cluster
            i64 min_tmp = this->min;
            Payload *p1_tmp = p1, *p2_temp = p2;
            if (x == this->min) {
                // no need to do nullptr check on summary because if there are at least 2 elements in the structure, the summary has been allocated
                i64 firstCluster = this->summary->min;
                x = INDEX(firstCluster, this->clusters[firstCluster]->min, ui);
                // this->min = x;
                // p1 = this->clusters[firstCluster]->p1;
                min_tmp = x;
                p1_tmp = this->clusters[firstCluster]->p1;
            }

            i64 h = HIGH(x, ui);

            i64 l = LOW(x, ui);
            bool erased;
            // now delete x from the cluster
            erased = this->clusters[h]->del(l);
            if (!erased && old_see_new)
                return false;
            else if (erased && old_see_new)
                errexit("del: erased && old_see_new");

            // if successfully deleted x and the cluster is empty now
            if (this->clusters[h]->min == -1) {
                // free the memory
                // delete this->clusters[h];
                // kToReclaim.push_back(this->clusters[h]);
                // this->clusters[h] = nullptr;  // absence of this line causes segfaults

                // update summary so that it reflects the emptiness
                erased = this->summary->del(h);
                if (!erased && old_see_new)
                    return false;
                else if (erased && old_see_new)
                    errexit("del: erased && old_see_new");
                if (p1_tmp != p1) {
                    if (min == min_tmp) errexit("del: min == min_tmp (#1)");
                    min = min_tmp;
                    pToRetire.push_back(p1);
                    p1 = p1_tmp;
                }

                // if we deleted the max element, we need to find the new max
                if (x == this->max) {
                    i64 summaryMax = this->summary->max;

                    // only 1 elem remaining
                    if (summaryMax == -1) {
                        if (!ds->check_epoch(p2)) {
                            old_see_new = true;
                            return false;
                        }
                        this->max = this->min;
                        pToRetire.push_back(p2);
                        p2 = p1;
                    } else {
                        if (!ds->check_epoch(p2)) {
                            old_see_new = true;
                            return false;
                        }
                        this->max = INDEX(summaryMax, this->clusters[summaryMax]->max, ui);
                        pToRetire.push_back(p2);
                        p2 = this->clusters[summaryMax]->p2;
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
                if (p1_tmp != p1) {
                    if (min == min_tmp) errexit("del: min == min_tmp (#2)");
                    min = min_tmp;
                    pToRetire.push_back(p1);
                    p1 = p1_tmp;
                }
                if (!ds->check_epoch(p2)) {
                    old_see_new = true;
                    return false;
                }
                this->max = INDEX(h, this->clusters[h]->max, ui);
                pToRetire.push_back(p2);
                p2 = this->clusters[h]->p2;
            } else {
                if (p1_tmp != p1) {
                    if (min == min_tmp) errexit("del: min == min_tmp (#3)");
                    min = min_tmp;
                    pToRetire.push_back(p1);
                    p1 = p1_tmp;
                }
            }
            return erased;
        }

        bool member(i64 x, i64 key) {
            // if (x == this->min || x == this->max) {
            if (p1 && p2) {
                if (key == p1->get_key(ds) || key == p2->get_key(ds)) {
                    return true;
                }
            } else if (!p1 ^ !p2)
                errexit("member: p1 ^ p2");
            if (this->u == 2) {
                // if it's neither min nor max, and we can't recurse any further, we're done
                return false;
            }
            UniverseInfo ui = kMap[this->u];

            i64 h = HIGH(x, ui);

            return this->clusters[h]->member(LOW(x, ui), key);
        }
    };

    Node *root;

    HTMvEBTree(GlobalTestConfig* gtc): Recoverable(gtc), root(new Node(HTMvEBTreeRange, this)) {
#ifdef VEB_TEST
        bitmap = new char[HTMvEBTreeRange / 8 + 1]();
        for (i64 i = 0; i < HTMvEBTreeRange / 8 + 1; ++i)
            bitmap[i] = 0;
#endif /* VEB_TEST */
    }

    ~HTMvEBTree() {
        delete root;
#ifdef VEB_TEST
        delete[] bitmap;
#endif /* VEB_TEST */
    }

#ifdef VEB_TEST
    bool _insert(K key, Payload *p) {
        bool retval = root->insert(key, p);
        if (retval) {
            SET(key);
            if (!GET(key)) errexit("insert: SET(key) failed");
        } else {
            if (!GET(key)) errexit("insert: !GET(key) && already inserted");
        }
        return retval;
    }
#endif /* VEB_TEST */

    bool insert(K key, int tid) {
        bool retval = false;
        begin_op();
        Payload *p = pnew<Payload>(key);
#ifdef VEB_TEST
        TLE(_insert, key COMMA p);
#else
        TLE(root->insert, key COMMA p);
#endif /* VEB_TEST */
        if (!retval) pdelete(p);
        end_op();
        // if (retval) {
        //     for (auto it = kToRefill.begin(); it != kToRefill.end(); ++it) {
        //         i64 neededSize = *it;
        //         while (kPool[neededSize].size() < POINTERS_PER_POOL) {
        //             kPool[neededSize].push_back((void *)new Node(neededSize));
        //         }
        //     }
        //     kToRefill.clear();
        // }
        return retval;
    }

#ifdef VEB_TEST
    bool _remove(K key) {
        bool retval = root->del(key);
        if (retval) {
            CLR(key);
            if (GET(key)) errexit("remove: CLR(key) failed");
            return retval;
        } else {
            if (GET(key)) errexit("remove: GET(key) && already removed");
        }
        return retval;
    }
#endif /* VEB_TEST */

    bool remove(K key, int tid) {
        bool retval = false;
remove_retry:
        retval = false;
        old_see_new = false;
        pToRetire.clear();
        begin_op();
#ifdef VEB_TEST
        TLE(_remove, key);
#else
        TLE(root->del, key);
#endif /* VEB_TEST */
        if (!retval && old_see_new) {
            abort_op();
            goto remove_retry;
        } else if (retval && old_see_new)
            errexit("retval && old_see_new");
        else {
            // for (const void *p : pToRetire) pretire((Payload *)p);
            end_op();
        }

        // if (retval) {
        //     for (auto it = kToReclaim.begin(); it != kToReclaim.end(); ++it) {
        //         Node *temp = (Node *)*it;
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

    bool member(K key, int tid) {
        bool retval = false;
        begin_op();
        TLE(root->member, key COMMA key);
        end_op();
        return retval;
    }

    void initThread(const int tid) {
        Recoverable::init_thread(tid); 
        kMap.clear();
        populate_maps(HTMvEBTreeRange, false);

        // for (const auto &pair : kMap) {
        //     i64 _u = pair.first;
        //     UniverseInfo _ui = pair.second;
        //     // printf("kMap[%ld]={lowBits: %ld, highBits: %ld, nClusters: %ld, clusterSize: %ld}\n", _u, _ui.lowBits, _ui.highBits, _ui.nClusters, _ui.clusterSize);
        //     for (int i = 0; i < POINTERS_PER_POOL; ++i) {
        //         kPool[_u].push_back((void *)new Node(_u));
        //     }
        // }
    }

    // void deinitThread(const int tid) {
    // }

    bool get(K key, int tid) override { return true; }
    void put(K key, int tid) override {}

    int recover() override {
        errexit("recover of HTMvEBTree not implemented.");
        return 0;
    }
};

template <class T> 
class HTMvEBTreeFactory : public RideableFactory {
    Rideable* build(GlobalTestConfig* gtc) {
        return new HTMvEBTree<T>(gtc);
    }
};

#endif /* HTM_VEB_TREE_P */
