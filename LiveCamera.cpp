#include "LiveCamera.h"

#include <videolib/include/IVideoPacket.h>

LiveCamera::LiveCamera() : m_exit(false),
                           m_capturer(NULL),
                           m_hlslive(NULL)
{
    pthread_mutex_init(&m_mutex, NULL);
    pthread_cond_init(&m_cond, NULL);
}

LiveCamera::~LiveCamera()
{
    stop();

    pthread_cond_destroy(&m_cond);
    pthread_mutex_destroy(&m_mutex);
}

bool
LiveCamera::start(const void * arg)
{
    int camera = (int)arg;
    char cameraId[1024];

    unsigned long stream;

    m_hlslive = new HLSLive();

    if (!m_hlslive->build("E:\\nginx\\nginx-1.9.9\\html\\hls", ///< path
                          "camera",                            ///< output prefix
                          "camera.m3u8",                       ///< index file name
                          "camera/",                           ///< URI prefix
                          6,                                   ///< max segment duration
                          5))                                  ///< max segments
    {
        goto fail;
    }

    if (!getCamera(camera, cameraId))
    {
        goto fail;
    }

    m_capturer = CreateDSCapturer(this, true, cameraId);
    if (!m_capturer)
    {
        fprintf(stderr, "create h264 stream capturer failed.\n");
        goto fail;
    }

    stream = m_capturer->CaptureH26XStream(SV_H264_ES, 352, 288, 256, 352*288*3, 30 * 2, true); ///< key-interval: 2s
    if (stream == 0)
    {
        fprintf(stderr, "capture h264 stream failed.\n");
        goto fail;
    }

    spawn();

    return true;

fail:

    stop();

    return false;
}

bool
LiveCamera::getCamera(int camera, char id[1024])
{
    char camera_name[1024];
    char route_name[32][128];
    int  routes = sizeof(route_name) / sizeof(route_name[0]);

    CamCap caps[128];
    int  cam_caps = sizeof(caps) / sizeof(caps[0]);

    unsigned long session_id = 0;

    /* check for specified camera */
    for (int i = 0; i <= camera; ++i)
    {
        if (!EnumDSCams(camera_name, 1024, id, 1024, routes, route_name, cam_caps, caps, session_id))
        {
            fprintf(stderr, "camera %d not found.\n", camera);
            return false;
        }
    }

    return true;
}

void
LiveCamera::stop()
{
    m_exit = true;

    pthread_cond_signal(&m_cond);

    wait();

    while (!m_packets.empty())
    {
        IVideoPacket * packet = m_packets.front();
        m_packets.pop_front();

        packet->Release();
    }

    if (m_capturer)
    {
        DestroyCapturer(m_capturer);
        m_capturer = NULL;
    }

    if (m_hlslive)
    {
        m_hlslive->tear();
        m_hlslive = NULL;
    }
}

void
LiveCamera::svc()
{
    while (!m_exit)
    {
        IVideoPacket * packet = NULL;
        VideoStream  * stream = NULL;

        pthread_mutex_lock(&m_mutex);

        while (!m_exit && m_packets.empty())
        {
            pthread_cond_wait(&m_cond, &m_mutex);
        }

        if (!m_exit)
        {
            packet = m_packets.front();
            m_packets.pop_front();
        }

        pthread_mutex_unlock(&m_mutex);

        if (packet)
        {
            stream = (VideoStream *)packet->GetVideoStructure();

            m_hlslive->encode(stream->pStream,
                              stream->iStreamLen,
                              av_rescale_q(packet->GetTimeStamp() * 10, (AVRational){1, 1000000}, (AVRational){1, 90000}));

            packet->Release();
        }
    }
}

void __stdcall
LiveCamera::XCapturerOutput(IXCapturer * capturer, unsigned long streamId, IVideoPacket * packet)
{
    if (packet)
    {
        pthread_mutex_lock(&m_mutex);

        packet->AddRef();
        m_packets.push_back(packet);

        pthread_cond_signal(&m_cond);

        pthread_mutex_unlock(&m_mutex);
    }
}
