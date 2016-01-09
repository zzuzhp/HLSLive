#ifndef ___TSSEGMENTER_H___
#define ___TSSEGMENTER_H___

extern "C"
{
#include <libavformat/avformat.h>
}

#include "config.h"
#include "TsMuxer.h"

#include <list>

/////////////////////////////////////////////////////////////////////////////
////

struct OutdatedSegment
{
    OutdatedSegment(int segment, double expires)
    {
        this->segment = segment;
        this->expires = expires;
    }

    int segment;    /// segment number
    double expires; /// remove time
};

class TsSegmenter
{
public:

    TsSegmenter(const char * path,
                const char * output_prefix,
                const char * m3u8_file,
                const char * url_prefix,
                int32_t      duration,
                int32_t      segments);

    ~TsSegmenter();

    bool build(AVCodecContext * avctx);

    void tear();

    bool addframe(AVPacket * packet);

private:

    void segmentfilename(bool segment, void * arg, char buf[MAX_PATH]); ///< array is just informative

    bool start_new_file();

    void write_end_file(bool last);

    void update_index_file(bool end);

    void check_outdated_segments();

    void log_packet(const AVFormatContext * ctx, const AVPacket * packet, const char * tag);

private:

    const char        * m_path;

    const char        * m_outputprefix;

    const char        * m_m3u8file;

    const char        * m_urlprefix;

    int32_t             m_duration;

    int32_t             m_segments;

    int32_t             m_firstsegment;

    int32_t             m_lastsegment;

    double              m_segstarttime;

    double              m_prev_segment_time;

    char                m_outputfile[MAX_PATH];

    int32_t             m_frame;

    int64_t             m_lastts;

    AVStream          * m_stream;

    AVFormatContext   * m_avfctx;

    ts_bucket_t       * m_bucket;

    TsMuxer           * m_muxer;

    FILE              * m_tsfile;

    std::list<OutdatedSegment *>  m_castoff;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___TSSEGMENTER_H___
