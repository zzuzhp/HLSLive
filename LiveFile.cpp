#include "LiveFile.h"

extern "C"
{
#include <libavutil/file.h>
}

LiveFile::LiveFile() : m_exit(false),
                       m_hlslive(NULL),
                       m_filedata(NULL),
                       m_datasize(0)
{
    (void)gVideoTypeString;
}

LiveFile::~LiveFile()
{
    stop();
}

bool
LiveFile::start(const void * arg)
{
    int ret = 0;

    const char * file = (const char *)arg;

    m_hlslive = new HLSLive();

    if (!m_hlslive->build("E:\\nginx\\nginx-1.9.9\\html\\hls", ///< path
                          "camera",                            ///< output prefix
                          "camera.m3u8",                       ///< index file name
                          "camera/",                           ///< URI prefix
                          10,                                  ///< max segment duration
                          6))                                  ///< max segments
    {
        goto fail;
    }

    ret = ::av_file_map(file, &m_filedata, &m_datasize, 0, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "unable to map input file: %s\n", av_err2str(ret));
        goto fail;
    }

    spawn();

    return true;

fail:

    stop();

    return false;
}

void
LiveFile::stop()
{
    m_exit = true;

    wait();

    if (m_filedata)
    {
        ::av_file_unmap(m_filedata, m_datasize);
        m_filedata = NULL;
    }

    if (m_hlslive)
    {
        m_hlslive->tear();
        m_hlslive = NULL;
    }
}

void
LiveFile::svc()
{
    uint32_t streampos = 0;
    uint32_t streamlen = 0;

    while (!m_exit)
    {
        streamlen = m_datasize - streampos > 1024 ? 1024 : (m_datasize - streampos);

        m_hlslive->encode(m_filedata + streampos, streamlen, 0);

        streampos += streamlen;

        if (streampos == m_datasize)
        {
            streampos = 0;
        }

        Sleep(6);
    }
}
