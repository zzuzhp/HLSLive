#include "TsSegmenter.h"

#include <cstdio>
#include <cassert>
#include <windows.h>

extern "C"
{
#include <libavutil/timestamp.h>
#include <libavutil/time.h>
}

const AVRational g_tb = {1, 90000};

TsSegmenter::TsSegmenter(const char * path,
                         const char * output_prefix,
                         const char * m3u8_file,
                         const char * url_prefix,
                         int32_t      duration,
                         int32_t      segments) : m_path(path),
                                                  m_outputprefix(output_prefix),
                                                  m_m3u8file(m3u8_file),
                                                  m_urlprefix(url_prefix),
                                                  m_duration(duration),
                                                  m_segments(segments),
                                                  m_firstsegment(0),
                                                  m_lastsegment(0),
                                                  m_segstarttime(0),
                                                  m_prev_segment_time(-1),
                                                  m_frame(0),
                                                  m_lastts(0),
                                                  m_stream(NULL),
                                                  m_avfctx(NULL),
                                                  m_bucket(NULL),
                                                  m_muxer(NULL)
{

}

TsSegmenter::~TsSegmenter()
{
    tear();
}

bool
TsSegmenter::build(AVCodecContext * avctx)
{
    int ret = 0;

#if !USE_FFMPEG_MUXER
    m_bucket = bucket_init();
    if (!m_bucket)
    {
        assert(0);
        goto fail;
    }

    m_muxer = new TsMuxer();
    if (!m_muxer || !m_muxer->build(true, false))
    {
        assert(0);
        goto fail;
    }
#else
    if (!avctx)
    {
        assert(0);
        goto fail;
    }

    ret = ::avformat_alloc_output_context2(&m_avfctx, NULL, "mpegts", NULL);
    if (ret < 0)
    {
        ///< could not create output context
        fprintf(stderr, "avformat alloc output context failed: %s\n", av_err2str(ret));
        goto fail;
    }

    m_stream = ::avformat_new_stream(m_avfctx, avctx->codec);
    if (!m_stream)
    {
        ///< failed creating output stream
        goto fail;
    }

    ///< set output stream time_base 90khz clock
    m_stream->time_base = g_tb;

    ret = ::avcodec_copy_context(m_stream->codec, avctx);
    if (ret < 0)
    {
        ///< failed to copy context from input to output stream codec context
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        goto fail;
    }

    m_stream->codec->width     = 1;
    m_stream->codec->height    = 1; //<! (for 'USE_FFMPEG_PARSER == 0')mpegts needs a width/height or ts muxer will fail to init.(don't know why).
    m_stream->codec->codec_tag = 0;

    if (m_avfctx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        m_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    ::av_dump_format(m_avfctx, 0, NULL, 1);
#endif // USE_FFMPEG_MUXER

    return true;

fail:

    tear();

    return false;
}

void
TsSegmenter::tear()
{
#if !USE_FFMPEG_MUXER
    if (m_muxer)
    {
        m_muxer->tear();

        delete m_muxer;
        m_muxer = NULL;
    }

    if (m_bucket)
    {
        bucket_destroy(m_bucket);
        m_bucket = NULL;
    }
#else
    if (m_avfctx)
    {
        write_end_file(true);

        ::avformat_free_context(m_avfctx);
        m_avfctx = NULL;
    }
#endif // USE_FFMPEG_MUXER

    while (!m_castoff.empty())
    {
        OutdatedSegment * segment = m_castoff.front();
        m_castoff.pop_front();

        delete segment;
    }
}

void
TsSegmenter::segmentfilename(bool segment, void * arg, char buf[MAX_PATH])
{
    if (segment)
    {
        snprintf(buf, MAX_PATH, "%s\\%s\\%s-%u.ts", m_path, m_urlprefix, m_outputprefix, (int32_t)arg);
    }
    else
    {
        snprintf(buf, MAX_PATH, "%s\\%s", m_path, (const char *)arg);
    }
}

bool
TsSegmenter::start_new_file()
{
    int ret;

    segmentfilename(true, (void *)m_lastsegment, m_outputfile);

#if !USE_FFMPEG_MUXER
    m_tsfile = fopen(m_outputfile, "wb");

    assert(m_tsfile);
    if (!m_tsfile)
    {
        return false;
    }

    m_muxer->write_header(m_bucket);
#else
    ret = ::avio_open(&m_avfctx->pb, m_outputfile, AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        fprintf(stderr, "could not open file '%s': %s\n", m_outputfile, av_err2str(ret));
        return false;
    }

    ///< write a new header at the start of each file
    ret = ::avformat_write_header(m_avfctx, NULL);
    if (ret < 0)
    {
        ///< error occurred when opening output file
        fprintf(stderr, "could not write mpegts header to first output file: %s\n", av_err2str(ret));
        return false;
    }
#endif

    return true;
}

void
TsSegmenter::write_end_file(bool last)
{
    ///< close ts file and free memory
#if !USE_FFMPEG_MUXER
    if (m_tsfile)
    {
        fflush(m_tsfile);
        fclose(m_tsfile);

        m_tsfile = NULL;
    }
#else
    if (m_avfctx->pb)
    {
        av_write_trailer(m_avfctx);

        avio_flush(m_avfctx->pb);
        avio_close(m_avfctx->pb);

        m_avfctx->pb = NULL;
    }
#endif

    ///< write m3u8 file
    update_index_file(last);
}

void
TsSegmenter::update_index_file(bool end)
{
#define WRITEBUF_LEN 4096
    FILE * indexfile;
    char tmpfilename[MAX_PATH], m3u8file[MAX_PATH];

    char writebuf[WRITEBUF_LEN];
    int i;

    segmentfilename(false, (void *)"HLSTsEncodeIndexTmp.txt", tmpfilename);
    segmentfilename(false, (void *)m_m3u8file, m3u8file);

    indexfile = fopen(tmpfilename, "w");
    if (!indexfile)
    {
        fprintf(stderr, "could not open temporary index file, index file will not be updated.\n");
        return;
    }

    if (m_segments != 0)
    {
        snprintf(writebuf, WRITEBUF_LEN, "#EXTM3U\n#EXT-X-TARGETDURATION:%u\n#EXT-X-MEDIA-SEQUENCE:%u\n", m_duration, m_firstsegment);
    }
    else
    {
        snprintf(writebuf, WRITEBUF_LEN, "#EXTM3U\n#EXT-X-TARGETDURATION:%u\n", m_duration);
    }

    if (fwrite(writebuf, strlen(writebuf), 1, indexfile) != 1)
    {
        fprintf(stderr, "could not write to index file, index file will not be updated.\n");
        fclose(indexfile);

        return;
    }

    for (i = m_firstsegment; i <= m_lastsegment; ++i)
    {
        snprintf(writebuf, WRITEBUF_LEN, "#EXTINF:%u,\n%s%s-%u.ts\n", m_duration, m_urlprefix, m_outputprefix, i);

        if (fwrite(writebuf, strlen(writebuf), 1, indexfile) != 1)
        {
            fprintf(stderr, "could not write to index file, index file will not be updated.\n");
            fclose(indexfile);

            return;
        }
    }

    if (end)
    {
        snprintf(writebuf, WRITEBUF_LEN, "#EXT-X-ENDLIST\n");

        if (fwrite(writebuf, strlen(writebuf), 1, indexfile) != 1)
        {
            fprintf(stderr, "could not write last file and endlist tag to index file, index file will not be updated.\n");
            fclose(indexfile);

            return;
        }
    }

    fclose(indexfile);

#if _WIN32
    (void)::remove(m3u8file);
#endif
    ///< __linux__   : linux
    ///< __MINGW32__ : mingw on windows

    if (::rename(tmpfilename, m3u8file) < 0)
    {
        ///< function prototype: 'int rename(const char *src_file, const char *dest_file);'
        ///< If the file referenced by dest_file exists prior to calling rename(), the behavior is implementation-defined.
        ///< On POSIX systems, the destination file is removed. On Windows systems, the rename() fails.
        fprintf(stderr, "could not rename index file, index file will not be updated.\n");
    }
}

void
TsSegmenter::check_outdated_segments()
{
    char removefile[MAX_PATH];

    while (!m_castoff.empty())
    {
        OutdatedSegment * segment = m_castoff.front();

        if (segment->expires > av_gettime())
        {
            break;
        }

        segmentfilename(true, (void *)segment->segment, removefile);

        ::remove(removefile);

        m_castoff.pop_front();

        delete segment;
    }
}

bool
TsSegmenter::addframe(AVPacket * packet)
{
    int ret;

    double segment_time;

    ts_buf_t * ts_buf = NULL;

    /* remove outdated segments */
    check_outdated_segments();

    /* copy packet */
    /// ::av_packet_rescale_ts(packet, *tb, m_stream->time_base); ///< not necessary, we give dts/pts manually below.

    /* mpegts needs a valid timestamp */
    if (packet->pts == AV_NOPTS_VALUE)
    {
#if WALLCLOCK_TS
        packet->pts = av_rescale_q(av_gettime(), (AVRational){1, 1000000}, g_tb);
#else
        packet->pts = av_rescale_q(m_frame++ * (1000000 / 29.97), (AVRational){1, 1000000}, g_tb);
#endif // WALLCLOCK_TS

        ///< it happens that successive 2 packets get exactly the same time when 'WALLCLOCK_TS' is opened.
        if (packet->pts <= m_lastts)
        {
            ///< make pts/dts monotonously increasing.
            packet->pts = m_lastts + av_rescale_q(1000000 / 120, (AVRational){1, 1000000}, g_tb);
        }

        packet->dts = packet->pts;
        packet->pos = -1;
    }

    m_lastts = packet->pts;

#if !SUPPRESS_LOGS
    log_packet(m_avfctx, packet, "out");
#endif // SUPPRESS_LOGS

    segment_time = packet->pts * av_q2d(g_tb);

    if (packet->flags & AV_PKT_FLAG_KEY)
    {
        ///< check for segment boundaries
        if (m_prev_segment_time < 0)
        {
            ///< first packet
            m_firstsegment = m_lastsegment = 0;

            start_new_file();

            m_prev_segment_time = segment_time;
        }
        else
        {
            if (segment_time - m_prev_segment_time > m_duration)
            {
                ///< remove outdated files
                if (m_lastsegment - m_firstsegment + 1 > m_segments)
                {
                    /* 6.2.2 When the server removes a Media Segment URI from the Playlist, the
                       corresponding Media Segment MUST remain available to clients for a
                       period of time equal to the duration of the segment plus the duration
                       of the longest Playlist file distributed by the server containing
                       that segment.  Removing a Media Segment earlier than that can
                       interrupt in-progress playback.
                     */
                    m_castoff.push_back(new OutdatedSegment(m_firstsegment, av_gettime() + m_duration * 3 * 1000000));

                    ++m_firstsegment;
                }

                ///< new segment
                write_end_file(false);

                ++m_lastsegment;

                ///< start writing to next segment
                start_new_file();

                m_prev_segment_time = segment_time;
            }
        }
    }

    if (m_prev_segment_time < 0)
    {
        ///< stream should start with i frame.
        fprintf(stderr, "drop a leading non-key frame at stream starts.\n");
        return false;
    }

    if (segment_time - m_prev_segment_time > m_duration)
    {
        ///< no new key frame is found before the segment exceeds the duration.
        ///< since modification of EXT-X-TARGETDURATION is forbidden, we have to drop these packets.
        fprintf(stderr, "segment time %f exceeds specified duration(%d), packet is dropped.\n", segment_time - m_prev_segment_time, m_duration);
        return false;
    }

    /* muxing this packet */
#if !USE_FFMPEG_MUXER
    m_muxer->write_video_packet(m_bucket, packet->dts, packet->pts, packet->data, packet->data + packet->size);

    while (ts_buf = bucket_retrieve(m_bucket))
    {
        fwrite(ts_buf->pos, 1, ts_buf->last - ts_buf->pos, m_tsfile);

        free(ts_buf->pos);
        free(ts_buf);
    }
#else
    ret = ::av_interleaved_write_frame(m_avfctx, packet);
    if (ret < 0)
    {
        fprintf(stderr, "write frame error: %s\n", av_err2str(ret));
        return false;
    }
#endif

    return true;
}

void
TsSegmenter::log_packet(const AVFormatContext * ctx, const AVPacket * packet, const char * tag)
{
    AVRational *time_base = &ctx->streams[packet->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(packet->pts),      av_ts2timestr(packet->pts, time_base),
           av_ts2str(packet->dts),      av_ts2timestr(packet->dts, time_base),
           av_ts2str(packet->duration), av_ts2timestr(packet->duration, time_base),
           packet->stream_index);
}
