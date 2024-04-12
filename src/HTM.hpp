#ifndef HTM_HPP
#define HTM_HPP

#include <immintrin.h>

class GlobalLock
{
private:
    volatile int _flag = false;
    volatile int *flag = &_flag;
public:
    void lock()
    {
        while (1) {
            if (*flag) {
                __asm__ __volatile__("pause;");
                continue;
            }
            if (__sync_bool_compare_and_swap(flag, false, true)) {
                return;
            }
        }
    }

    void unlock()
    {
        __asm__ __volatile__("" ::: "memory");
        *flag = false;
    }

    bool isLocked()
    {
        return *flag;
    }
};

#endif // HTM_HPP
