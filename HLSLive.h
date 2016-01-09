#ifndef ___HLSLIVE_H___
#define ___HLSLIVE_H___

#include "config.h"
#include "IOContext.h"
#include "AVCReader.h"
#include "TsSegmenter.h"
#include "ThreadBase.h"

extern "C"
{
    #include <libavformat/avformat.h>
}

#include <videolib/include/IXRender.h>
#include <pthread.h>

/////////////////////////////////////////////////////////////////////////////
////

class HLSLive :
#if NO_RELAY
public IOutputContextSink,
#endif
public ThreadBase
{
public:

    HLSLive();

    ~HLSLive();

    bool build(const char * path,
               const char * output_prefix,
               const char * m3u8_file,
               const char * url_prefix,
               int32_t      duration,
               int32_t      segments);

    void tear();

    bool encode(unsigned char * data, int len, unsigned long timestamp);

    void svc();

private:

    bool createInputContext();

    bool createOutputContext();

    void log_packet(const AVRational * time_base, const AVPacket * packet, const char * tag);

    void OCtxOutput(uint8_t * data, int32_t len);

private:

    enum {INPUT_STREAM_IDC = 0};

    volatile bool     m_exit;

    IOContext       * m_inio;

    IOContext       * m_outio;

    AVCReader       * m_avcreader;

    AVCodecContext  * m_iavctx;

    AVFormatContext * m_inputctx;

    AVFormatContext * m_outputctx;

    TsSegmenter     * m_segmenter;

#if OPEN_PREVIEW
    IXRenderer      * m_renderer;
#endif
};

/////////////////////////////////////////////////////////////////////////////
////

#endif ///< ___HLSLIVE_H___
