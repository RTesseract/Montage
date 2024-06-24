#ifndef GLOBAL_LOCK_HPP
#define GLOBAL_LOCK_HPP

static void acquireLock(volatile int *lock)
{
    while (true)
    {
        if (*lock)
        {
            __asm__ __volatile__("pause;");
            continue;
        }
        if (__sync_bool_compare_and_swap(lock, false, true))
            return;
    }
}

static void releaseLock(volatile int *lock)
{
    __asm__ __volatile__("" ::: "memory");
    *lock = false;
}

static bool readLock(volatile int *lock)
{
    return *lock;
}

#endif /* GLOBAL_LOCK_HPP */
