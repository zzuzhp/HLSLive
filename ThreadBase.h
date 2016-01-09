#ifndef ___THREADBASE_H___
#define ___THREADBASE_H___

#include <pthread.h>

/////////////////////////////////////////////////////////////////////////////
////

class ThreadBase
{
protected:

    ThreadBase();

    virtual ~ThreadBase();

    bool spawn();

    void wait();

    virtual void svc() = 0;

private:

    static void * svcRun(void * arg);

private:

    ThreadBase(const ThreadBase &);

    ThreadBase& operator=(const ThreadBase &);

private:

    bool            m_running_b;

    pthread_cond_t  m_cond_b;

    pthread_mutex_t m_mutex_b;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___THREADBASE_H___
