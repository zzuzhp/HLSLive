#ifndef ___LIVEFILE_H___
#define ___LIVEFILE_H___

#include "config.h"
#include "HLSLive.h"
#include "ThreadBase.h"
#include "ILive.h"

#include <windows.h>

/////////////////////////////////////////////////////////////////////////////
////

class LiveFile : public ILive,
                 public ThreadBase
{
public:

    LiveFile();

    ~LiveFile();

    bool start(const void * arg);

    void stop();

private:

    void svc();

private:

    volatile bool   m_exit;

    HLSLive       * m_hlslive;

    uint8_t       * m_filedata;

    size_t          m_datasize;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___LIVEFILE_H___
