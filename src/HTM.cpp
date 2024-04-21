#include "HTM.hpp"

const int htmMaxRetriesLeft = 35;
const unsigned htmMaxPauseTimes = 2;

void GlobalLock::lock()
{
    while (flag || !__sync_bool_compare_and_swap(&flag, false, true))
        __asm__ __volatile__("pause;");
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

bool txn_cas(int *ptr, int old, int _new)
{}
