#include "HTM.hpp"

const int htmMaxRetriesLeft = 35;
const unsigned htmMaxPauseTimes = 2;

void GlobalLock::lock()
{
    while (true)
    {
        while (flag)
            __asm__ __volatile__("pause;");
        if (__sync_bool_compare_and_swap(&flag, false, true))
            return;
    }
}

void GlobalLock::unlock()
{
    __asm__ __volatile__("" ::: "memory");
    flag = false;
}

bool GlobalLock::isLocked() const
{
    return flag;
}
