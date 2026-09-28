/*
 * Copyright (c) 2018-2021, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021, kleines Filmröllchen <malu.bertsch@gmail.com>
 * Copyright (c) 2025, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Assertions.h>
#include <AK/Noncopyable.h>
#include <pthread.h>

namespace AK {

// NOTE: Upstream's AK/Mutex.h has recursive and interprocess mutexes and condition variables as well (it came
//       from LibSync). This tree has what the tables of interned strings need, under the same names.
class Mutex {
    AK_MAKE_NONCOPYABLE(Mutex);
    AK_MAKE_NONMOVABLE(Mutex);

public:
    Mutex()
    {
        auto result = pthread_mutex_init(&m_mutex, nullptr);
        VERIFY(result == 0);
    }

    ~Mutex()
    {
        (void)pthread_mutex_destroy(&m_mutex);
    }

    bool try_lock()
    {
        return pthread_mutex_trylock(&m_mutex) == 0;
    }

    void lock()
    {
        auto result = pthread_mutex_lock(&m_mutex);
        VERIFY(result == 0);
    }

    void unlock()
    {
        auto result = pthread_mutex_unlock(&m_mutex);
        VERIFY(result == 0);
    }

private:
    pthread_mutex_t m_mutex;
};

template<typename MutexType>
class [[nodiscard]] MutexLocker {
    AK_MAKE_NONCOPYABLE(MutexLocker);
    AK_MAKE_NONMOVABLE(MutexLocker);

public:
    ALWAYS_INLINE explicit MutexLocker(MutexType& mutex)
        : m_mutex(mutex)
    {
        lock();
    }
    ALWAYS_INLINE ~MutexLocker()
    {
        unlock();
    }
    ALWAYS_INLINE void unlock() { m_mutex.unlock(); }
    ALWAYS_INLINE void lock() { m_mutex.lock(); }

private:
    MutexType& m_mutex;
};

template<typename MutexType>
MutexLocker(MutexType&) -> MutexLocker<MutexType>;

}
