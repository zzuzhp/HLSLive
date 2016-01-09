#include "ThreadBase.h"
#include <cassert>

ThreadBase::ThreadBase() : m_running_b(false)
{
    pthread_mutex_init(&m_mutex_b, NULL);
    pthread_cond_init(&m_cond_b, NULL);
}

ThreadBase::~ThreadBase()
{
    assert(!m_running_b);

    pthread_cond_destroy(&m_cond_b);
    pthread_mutex_destroy(&m_mutex_b);
}

bool
ThreadBase::spawn()
{
    pthread_mutex_lock(&m_mutex_b);

    pthread_t threadId;
    const int ret = pthread_create(&threadId, NULL, &ThreadBase::svcRun, this);

    if (ret == -1)
    {
        pthread_mutex_unlock(&m_mutex_b);
        return false;
    }

    m_running_b = true;
    pthread_mutex_unlock(&m_mutex_b);

    return true;
}

void
ThreadBase::wait()
{
    pthread_mutex_lock(&m_mutex_b);
    while (m_running_b)
    {
        ///< the mutex param of 'pthread_cond_wait()' MUST not be null, and MUST be LOCKED.
        pthread_cond_wait(&m_cond_b, &m_mutex_b);
    }
    pthread_mutex_unlock(&m_mutex_b);
}

void *
ThreadBase::svcRun(void * arg)
{
    ThreadBase * const thread = (ThreadBase *)arg;

    thread->svc();

    pthread_mutex_lock(&thread->m_mutex_b);
    thread->m_running_b = false;
    pthread_cond_signal(&thread->m_cond_b);
    pthread_mutex_unlock(&thread->m_mutex_b);

    return 0;
}
