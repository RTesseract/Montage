#ifndef HTM_HPP
#define HTM_HPP

#include <immintrin.h>

extern const int htmMaxRetriesLeft;
extern const unsigned htmMaxPauseTimes;

class GlobalLock
{
private:
    volatile bool _flag = false;
    volatile bool *flag = &_flag;
public:
    void lock();
    void unlock();
    bool isLocked() const;
};

inline void htmWait()
{
    for (unsigned htmPauseTimes = 0; htmPauseTimes < htmMaxPauseTimes; ++htmPauseTimes)
        _mm_pause();
}

#endif // HTM_HPP
