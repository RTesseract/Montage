#ifndef VEB_K_NOCUTOFF_IMPL_H
#define VEB_K_NOCUTOFF_IMPL_H

#include <immintrin.h>
#include <stdio.h>

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
thread_local map<i64, UniverseInfo> kMap;     // for key-only structures
// thread_local map<i64, vector<void *>> kPool;  // for key-only structures
// thread_local vector<i64> kToRefill;           // for key-only structures
// thread_local vector<void *> kToReclaim;       // for key-only structures

#define CUTOFF 2
#define CUTOFF_POWER 0

class VebKNaive {
   public:
    i64 u;
    VebKNaive **clusters;
    VebKNaive *summary;
    // PAD;
    volatile i64 min;
    volatile i64 max;
    // PAD;
    // Constructor

    VebKNaive(i64 u) {
        this->u = u;
        this->min = -1;
        this->max = -1;

        if (u == 2) {
        } else {

            UniverseInfo ui;

            if (kMap.find(u) != kMap.end()) {
                ui = kMap[u];
            } else {
                ui = divide_node(u, CUTOFF_POWER, false);
            }

            this->summary = new VebKNaive(ui.nClusters);

            this->clusters = new VebKNaive *[ui.nClusters];
            for (i64 i = 0; i < ui.nClusters; ++i) { 
                this->clusters[i] = new VebKNaive(ui.clusterSize);
            }
        }
    }

    void makeMeReusable() {
        this->min = -1;
        this->max = -1;
        //
    }

    ~VebKNaive() {
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

    void initThread(const int tid) {
        kMap.clear();
        this->populate_maps(this->u, false);

        // for (const auto &pair : kMap) {
        //     i64 _u = pair.first;
        //     UniverseInfo _ui = pair.second;
        //     // printf("kMap[%ld]={lowBits: %ld, highBits: %ld, nClusters: %ld, clusterSize: %ld}\n", _u, _ui.lowBits, _ui.highBits, _ui.nClusters, _ui.clusterSize);
        //     for (int i = 0; i < POINTERS_PER_POOL; ++i) {
        //         kPool[_u].push_back((void *)new VebKNaive(_u));
        //     }
        // }
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

    i64 successor(i64 x) {
        if (this->u == 2) {
            if (x == 0 && this->max == 1) {
                return 1;
            }
            return -1;
        }

        // the query is smaller than the minimum => return the minimum
        if (this->min != -1 && x < this->min) {
            return this->min;
        }

        UniverseInfo ui = kMap[this->u];
        i64 h = HIGH(x, ui);

        if (this->clusters[h]->min != -1) {
            i64 l = LOW(x, ui);

            // maximum in current cluster
            i64 maxLow = this->clusters[h]->max;

            // a maxLow exists, and it is bigger than our low
            // => a successor exists in current cluster
            if (maxLow != -1 && l < maxLow) {
                i64 offset = this->clusters[h]->successor(l);
                return INDEX(h, offset, ui);
            }
        }   

        // if there is no summary, there is no successor
        if (this->summary->min == -1) {
            return -1;
        }

        // maxLow doesn't exist
        // we should find the successor in the next clusters
        i64 succCluster = this->summary->successor(h);

        // nothing found in summary
        // => successor does not exist
        if (succCluster == -1) {
            return -1;
        }

        // succCluster exists, now find the minimum in there
        i64 offset = this->clusters[succCluster]->min;

        return INDEX(succCluster, offset, ui);
    }

    i64 predecessor(i64 x) {
        if (this->u == 2) {
            if (x == 1 && this->min == 0) {
                return 0;
            }
            return -1;
        }

        // the query is bigger than the maximim => return the maximum
        if (this->max != -1 && x > this->max) {
            return this->max;
        }

        UniverseInfo ui = kMap[this->u];
        i64 h = HIGH(x, ui);

        if (this->clusters[h] != nullptr) {
            i64 minLow = this->clusters[h]->min;
            i64 l = LOW(x, ui);

            // a minLow exists, and it is smaller than our low
            // => a predecessor exists in current cluster
            if (minLow != -1 && minLow < l) {
                i64 offset = this->clusters[h]->predecessor(l);
                return INDEX(h, offset, ui);
            }
            
        }


        // if there is no summary, there is no predecessor
        if (this->summary == nullptr) {
            return -1;
        }

        // minLow doesn't exist
        // we should find the predecessor in the previous clusters
        i64 predCluster = this->summary->predecessor(h);

        // nothing found in summary
        // THIS PART DIFFERS FROM THE SUCCESSOR IMPLEMENTATION
        if (predCluster == -1) {
            // predecessor might be stored in a min field somewhere
            // and we didn't see it because min is not stored in any clusters
            if (this->min != -1 && x > this->min) {
                return this->min;
            }

            // not found in summary, and minimum doesn't exist (or isn't less than x)
            return -1;
        }

        i64 offset = this->clusters[predCluster]->max;
        return INDEX(predCluster, offset, ui);
    }

    bool member(i64 x) {
        if (x == this->min || x == this->max) {
            return true;
        }
        if (this->u == 2) {
            // if it's neither min nor max, and we can't recurse any further, we're done
            return false;
        }
        UniverseInfo ui = kMap[this->u];

        i64 h = HIGH(x, ui);

        return this->clusters[h]->member(LOW(x, ui));
    }

    // void allocateClusterIfNeeded(i64 h) {
    //     if (this->clusters[h] == nullptr) {
    //         i64 clusterU = kMap[this->u].clusterSize;
    //         this->clusters[h] = (VebKNaive *)kPool[clusterU].back();
    //         kPool[clusterU].pop_back();
    //         kToRefill.push_back(clusterU);
    //     }
    // }

    // void allocateSummaryIfNeeded() {
    //     if (this->summary == nullptr) {
    //         i64 summaryU = kMap[this->u].nClusters;
    //         this->summary = (VebKNaive *)kPool[summaryU].back();
    //         kPool[summaryU].pop_back();
    //         kToRefill.push_back(summaryU);
    //     }
    // }

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

    bool memberDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(SEARCH, member, x);
        return retval;
    }

    bool insertDriver(const int tid, i64 x) {
        bool retval = false;
        TLE(INSERT, insert, x);
        // if (retval) {
        //     for (auto it = kToRefill.begin(); it != kToRefill.end(); ++it) {
        //         i64 neededSize = *it;
        //         while (kPool[neededSize].size() < POINTERS_PER_POOL) {
        //             kPool[neededSize].push_back((void *)new VebKNaive(neededSize));
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
        //         VebKNaive *temp = (VebKNaive *)*it;
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
#endif