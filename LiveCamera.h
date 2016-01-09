#ifndef ___LIVECAMERA_H___
#define ___LIVECAMERA_H___

#include <videolib/include/IXCapture.h>
#include <pthread.h>

#include "config.h"
#include "HLSLive.h"
#include "ThreadBase.h"
#include "ILive.h"

#include <list>

/////////////////////////////////////////////////////////////////////////////
////

class LiveCamera : public ILive,
                   public IXCapturerSink,
                   public ThreadBase
{
public:

    unsigned long __stdcall AddRef() {return 1;}

    unsigned long __stdcall Release() {return 1;}

    LiveCamera();

    ~LiveCamera();

    bool start(const void * arg);

    void stop();

private:

    void svc();

    bool getCamera(int camera, char id[1024]);

    void __stdcall XCapturerOutput(IXCapturer * capturer, unsigned long streamId, IVideoPacket * packet);

private:

    volatile bool     m_exit;

    IXCapturer      * m_capturer;

    HLSLive         * m_hlslive;

    std::list<IVideoPacket *> m_packets;

    pthread_cond_t    m_cond;

    pthread_mutex_t   m_mutex;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___LIVECAMERA_H___
