#include <cmath>
#include <cstdint>

typedef uint64_t ui64;
typedef int64_t i64;
typedef uint32_t ui32;
typedef int32_t i32;

typedef struct {
    i64 clusterSize;  // size of each cluster (lowerRoot)
    i64 nClusters;    // size of summary (nClusters) (upperRoot)
    i64 lowBits;      // (lowerPower)
    i64 highBits;     // (lowerRoot)
    i64 lowMask;      // clusterSize - 1
} UniverseInfo;

enum Operations {
    SEARCH,
    INSERT,
    DELETE,
    SUCCESSOR,
    PREDECESSOR
};

// for debugging purposes
typedef struct { 
    void *veb;
    // this is true for summary, and any clusters of the summary 
    // basically, it can be interpreted as isKeyOnly in kv structures
    bool isSummary;
    
    int depth; 
    i64 base; // to add to my keys's sum
} AugmentedVeb; 


AugmentedVeb *augmentNode(void *node) { 
    AugmentedVeb *res = (AugmentedVeb *)malloc(sizeof(AugmentedVeb));
    res->veb = (void *)node;
    res->isSummary = false;
    res->depth = 0; 
    res->base = 0;
    return res;
}



#define LOW(x, ui) ((x) & (ui.lowMask))
#define HIGH(x, ui) ((x) >> (ui.lowBits))

#ifndef BITWISE_INDEX
#define INDEX(cluster, offset, ui) ((cluster) * (ui.clusterSize) + (offset))
#else
#define INDEX(cluster, offset, ui) (((cluster) << (ui.lowBits)) + offset)
#endif

#ifndef POINTERS_PER_POOL
#define POINTERS_PER_POOL 5
#endif

#ifndef POOL_GROW_COEFFICIENT
#define POOL_GROW_COEFFICIENT 2
#endif

UniverseInfo divide_node(i64 u, int cutoffPower, bool isKV) {
    // first field is the number of clusters (size of summary)
    // second field of the size of each cluster
    UniverseInfo res;
    i64 lowerPower, lowerRoot, upperPower, upperRoot;

    double powers = log2(u);

    lowerPower = floor(powers / 2);
    upperPower = ceil(powers / 2);

#ifdef SHARD_24
    if (powers > 24) {
        lowerPower = 24;
        upperPower = powers - 24;
    } else if (powers < 24 && powers > 12) { 
        lowerPower = 12;
        upperPower = powers - 12;
    }
#endif

#ifdef FORCE_64_KV
    if (isKV) {
        while (lowerPower < cutoffPower && upperPower > 0) {
            lowerPower++;
            upperPower--;
        }
    }
#endif

#ifdef FORCE_64_K
    if (!isKV) {
        while (lowerPower < cutoffPower && upperPower > 0) {
            lowerPower++;
            upperPower--;
        }
    }
#endif

    lowerRoot = pow(2, lowerPower);  // size of each cluster
    upperRoot = pow(2, upperPower);  // nClusters, size of summary

    res.nClusters = upperRoot;
    res.highBits = upperPower;
    res.clusterSize = lowerRoot;
    res.lowBits = lowerPower;
    res.lowMask = lowerRoot - 1;

    return res;
}



#ifdef VEB_TRACK_GSTATS

#define TRACK_STATS_ATTEMPT(op) \
    GSTATS_ADD_IX(tid, veb_txn_attempts, 1, op);

#define TRACK_STATS_COMMIT(op) \
    GSTATS_ADD_IX(tid, veb_txn_commits, 1, op);

#define TRACK_STATS_ABORT(op) \
    GSTATS_ADD_IX(tid, veb_txn_aborts, 1, op);

#define TRACK_STATS_FALLBACK(op) \
    GSTATS_ADD_IX(tid, veb_txn_fallbacks, 1, op);

#else
#define TRACK_STATS_ATTEMPT(op)
#define TRACK_STATS_COMMIT(op)
#define TRACK_STATS_ABORT(op)
#define TRACK_STATS_FALLBACK(op)
#endif

#ifndef MAX_RETRIES
#define MAX_RETRIES 35
#endif

#ifndef PAUSE_COUNT
#define PAUSE_COUNT 2
#endif

#define PAUSE()                                      \
    for (int __pc = 0; __pc < PAUSE_COUNT; ++__pc) { \
        _mm_pause();                                 \
    }

#define TLE(op, func, args)                    \
    int retriesLeft = MAX_RETRIES;             \
    int txnStatus;                             \
    retry:                                     \
    TRACK_STATS_ATTEMPT(op)                    \
    txnStatus = _xbegin();                     \
    if (txnStatus == _XBEGIN_STARTED) {        \
        if (readLock(&globalLock)) _xabort(0); \
        retval = this->func(args);             \
        _xend();                               \
        TRACK_STATS_COMMIT(op)                 \
    } else {                                   \
        TRACK_STATS_ABORT(op)                  \
        while (readLock(&globalLock)) {        \
            PAUSE();                           \
        }                                      \
        if (--retriesLeft > 0) goto retry;     \
        acquireLock(&globalLock);              \
        retval = this->func(args);             \
        releaseLock(&globalLock);              \
        TRACK_STATS_FALLBACK(op)               \
    }

