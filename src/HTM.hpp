#ifndef HTM_HPP
#define HTM_HPP

#include <immintrin.h>

extern const int htmMaxRetriesLeft;
extern const unsigned htmMaxPauseTimes;

class GlobalLock
{
private:
    volatile bool flag = false;
public:
    void lock();
    void unlock();
    bool isLocked() const;
};

inline void htmSpinWait(GlobalLock &lock)
{
    while (lock.isLocked())
        for (unsigned htmPauseTimes = 0; htmPauseTimes < htmMaxPauseTimes; ++htmPauseTimes)
            _mm_pause();
}

#endif // HTM_HPP
