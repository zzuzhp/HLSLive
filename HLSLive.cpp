#include "HLSLive.h"
#include "IOContext.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
}

#include <videolib/include/IVideoPacket.h>
#include <signal.h>
#include <cassert>

HLSLive::HLSLive() : m_exit(false),
                     m_inio(NULL),
                     m_outio(NULL),
                     m_avcreader(NULL),
                     m_iavctx(NULL),
                     m_inputctx(NULL),
                     m_outputctx(NULL),
                     m_segmenter(NULL)
{

}

HLSLive::~HLSLive()
{
    tear();
}

void
HLSLive::tear()
{
    ///< clean up
    m_exit = true;

    wait();

#if !USE_FFMPEG_PARSER
    if (m_avcreader)
    {
        m_avcreader->tear();

        delete m_avcreader;
        m_avcreader = NULL;
    }
#else
    if (m_inio)
    {
        m_inio->prepareQuit();
    }
#endif // USE_FFMPEG_PARSER

#if NO_RELAY
    if (m_outio)
    {
        m_outio->prepareQuit();
    }
#endif

#if USE_FFMPEG_PARSER
    if (m_inputctx)
    {
        ::avformat_close_input(&m_inputctx);
        ::avformat_free_context(m_inputctx);

        m_inputctx = NULL;
    }
#endif

#if NO_RELAY
    if (m_outputctx)
    {
        ::avformat_free_context(m_outputctx);
        m_outputctx = NULL;
    }
#endif

#if USE_FFMPEG_PARSER
    if (m_inio)
    {
        destroyIOContext(m_inio);
        m_inio = NULL;
    }
#endif // USE_FFMPEG_PARSER

#if NO_RELAY
    if (m_outio)
    {
        destroyIOContext(m_outio);
        m_outio = NULL;
    }
#else
    if (m_segmenter)
    {
        delete m_segmenter;
        m_segmenter = NULL;
    }
#endif // NO_RELAY

#if OPEN_PREVIEW
    if (m_renderer)
    {
        DestroyRenderer(m_renderer);
        m_renderer = NULL;
    }
#endif // OPEN_PREVIEW
}

bool
HLSLive::build(const char * path,
               const char * output_prefix,
               const char * m3u8_file,
               const char * url_prefix,
               int32_t      duration,
               int32_t      segments)
{
    ::av_register_all(); ///< prepare the FFMPEG library

#if OPEN_PREVIEW
    m_renderer = CreateRenderer();
    if (!m_renderer)
    {
        fprintf(stderr, "create renderer failed.\n");
        goto fail;
    }
#endif // OPEN_PREVIEW

#if !USE_FFMPEG_PARSER
    m_avcreader = new AVCReader();
    if (!m_avcreader ||! m_avcreader->build(false))
    {
        ///< could not create the avc reader
        fprintf(stderr, "create avc reader failed.\n");
        goto fail;
    }
#else
    m_inio = createIOContext(true);
    if (!m_inio)
    {
        ///< could not create custom output IO context
        assert(0);
        goto fail;
    }
#endif

#if NO_RELAY
    m_outio = createIOContext(false, (IOutputContextSink *)this);
    if (!m_outio)
    {
        ///< could not create custom output IO context
        assert(0);
        goto fail;
    }
#else
    m_segmenter = new TsSegmenter(path, output_prefix, m3u8_file, url_prefix, duration, segments);
#endif

    spawn();

    return true;

fail:

    tear();

    return false;
}

bool
HLSLive::encode(unsigned char * data, int len, unsigned long ts)
{
#if !USE_FFMPEG_PARSER
    if (!m_avcreader || !m_avcreader->parse(data, len))
    {
        return false;
    }
#else
    if (!m_inio || !m_inio->addbuffer(data, len, ts))
    {
        return false;
    }
#endif

    return true;
}

void
HLSLive::OCtxOutput(uint8_t * data, int32_t len)
{
    static FILE * file = fopen("dumpTs.ts", "wb");
    fwrite(data, 1, len, file);
    fflush(file);
}

void
HLSLive::svc()
{
    AVPacket packet;
    AVRational itb = {1, 90000};

#if !USE_FFMPEG_PARSER
    unsigned char * data;
    int len;
    bool key;
#endif

    int ret;

#if OPEN_PREVIEW
    IVideoPacket * xpacket = NULL;
    VideoStream xstream;
#endif // OPEN_PREVIEW

    int frame = 0;

    (void)frame;

    if (!createInputContext())
    {
        return;
    }

#if NO_RELAY
    if (!createOutputContext())
    {
        return;
    }
#else
    if (!m_segmenter->build(m_iavctx))
    {
        return;
    }
#endif

    while (!m_exit)
    {
#if NO_RELAY
        AVStream * os = NULL;
#endif

#if USE_FFMPEG_PARSER
        ret = ::av_read_frame(m_inputctx, &packet);
        if (ret < 0)
        {
            fprintf(stderr, "read frame failed: %s\n", av_err2str(ret));
            break;
        }
#else
        if (!m_avcreader->getframe(&data, len, key, false))
        {
            Sleep(1);
            continue;
        }

        if (::av_new_packet(&packet, len) != 0)
        {
            fprintf(stderr, "av new packet failed.\n");
            break;
        }

        memcpy(packet.data, data, len);

        if (key)
        {
            packet.flags |= AV_PKT_FLAG_KEY;
        }

        delete[] data;
#endif

        if (packet.stream_index != INPUT_STREAM_IDC)
        {
            ::av_free_packet(&packet);
            continue;
        }

#if !SUPPRESS_LOGS
        log_packet(&itb, &packet, "in");
#endif // !SUPPRESS_LOGS

#if OPEN_PREVIEW
        xstream.pStream    = packet.data;
        xstream.iStreamLen = packet.size;
        xstream.bStart     = true;
        xstream.bEnd       = true;
        xstream.bKey       = false;

        xpacket = CreateVideoPacket(SV_H264_ES, &xstream, true);

        m_renderer->Render(xpacket);

        xpacket->Release();
#endif // OPEN_PREVIEW

#if NO_RELAY
        os = m_outputctx->streams[packet.stream_index];

        /* copy packet */
        ::av_packet_rescale_ts(&packet, &itb, os->time_base); ///< not necessary, we give dts/pts manually below.

        ///< mpegts needs a valid timestamp
        packet.pts = av_rescale_q(frame++ * (1000000 / 30.0), (AVRational){1, 1000000}, os->time_base);
        packet.dts = packet.pts;
        packet.pos = -1;

#if !SUPPRESS_LOGS
        log_packet(&os->time_base, &packet, "out");
#endif // !SUPPRESS_LOGS

        ret = ::av_interleaved_write_frame(m_outputctx, &packet);
        if (ret < 0)
        {
            ///< error muxing packet
            fprintf(stderr, "write frame failed: %s\n", av_err2str(ret));
        }
#else
        m_segmenter->addframe(&packet);
#endif

        ::av_free_packet(&packet);
    }

#if NO_RELAY
    ::av_write_trailer(m_outputctx);
#endif
}

bool
HLSLive::createInputContext()
{
    AVCodec * codec  = NULL;

    int ret = 0;

#if USE_FFMPEG_PARSER
    m_inputctx = ::avformat_alloc_context();
    if (!m_inputctx)
    {
        return false;
    }

    m_inputctx->pb = (AVIOContext *)*m_inio;

    av_opt_set_int(m_inputctx, "probesize", 1024, 0);
    av_opt_set_int(m_inputctx, "formatprobesize", 2048, 0);

    ret = ::avformat_open_input(&m_inputctx, NULL, NULL, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "avformat open input error: %s\n", av_err2str(ret));
        return false;
    }

    m_inputctx->iformat->flags |= AVFMT_NOFILE;
    av_opt_set_int(m_inputctx, "analyzeduration", 10, 0);

    ret = ::avformat_find_stream_info(m_inputctx, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "find stream info error: %s\n", av_err2str(ret));
        return false;
    }

    m_iavctx = m_inputctx->streams[0]->codec;

    /* dump the input stream */
    ::av_dump_format(m_inputctx, 0, 0, 0);
#else
    m_iavctx = avcodec_alloc_context3(NULL);
    if (!m_iavctx)
    {
        fprintf(stderr, "avcodec alloc context failed.");
        return false;
    }

    codec = avcodec_find_decoder(CODEC_ID_H264);
    if (!codec)
    {
        fprintf(stderr, "could not find video decoder.\n");
        return false;
    }

    if (avcodec_open2(m_iavctx, codec, NULL) < 0)
    {
        fprintf(stderr, "open video decoder failed.\n");
        return false;
    }
#endif

    return true;
}

bool
HLSLive::createOutputContext()
{
    AVStream * os = NULL;

    int ret = 0;

    if (!m_iavctx)
    {
        fprintf(stderr, "input context should be created before output context.\n");
        return false;
    }

    ret = ::avformat_alloc_output_context2(&m_outputctx, NULL, "mpegts", NULL);
    if (ret < 0)
    {
        ///< could not create output context
        fprintf(stderr, "alloc output context error: %s\n", av_err2str(ret));
        return false;
    }

    m_outputctx->pb = (AVIOContext *)*m_outio;

    os = ::avformat_new_stream(m_outputctx, m_iavctx->codec);
    if (!os)
    {
        ///< failed creating output stream
        return false;
    }

    ///< set output stream time_base 90khz clock
    os->time_base.num = 1;
    os->time_base.den = 90000;

    ret = ::avcodec_copy_context(os->codec, m_iavctx);
    if (ret < 0)
    {
        ///< failed to copy context from input to output stream codec context
        fprintf(stderr, "avcodec copy context error: %s\n", av_err2str(ret));
        return false;
    }

    os->codec->codec_tag = 0;

    if (m_outputctx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        os->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = ::avformat_write_header(m_outputctx, NULL);
    if (ret < 0)
    {
        ///< error occurred when opening output file
        fprintf(stderr, "write header error: %s\n", av_err2str(ret));
        return false;
    }

    ::av_dump_format(m_outputctx, 0, NULL, 1);

    return true;
}

void
HLSLive::log_packet(const AVRational * time_base, const AVPacket * packet, const char * tag)
{
    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(packet->pts),      av_ts2timestr(packet->pts, (AVRational *)time_base),
           av_ts2str(packet->dts),      av_ts2timestr(packet->dts, (AVRational *)time_base),
           av_ts2str(packet->duration), av_ts2timestr(packet->duration, (AVRational *)time_base),
           packet->stream_index);
}
