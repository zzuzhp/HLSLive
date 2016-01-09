#ifndef ___TSMUXER_H___
#define ___TSMUXER_H___

#include "TsBucket.h"

#include <stdint.h>

/////////////////////////////////////////////////////////////////////////////
////

/* for audio, a PES packet header is generated every MAX_PES_HEADER_FREQ packets, or when the dts delta is over AUDIO_DELTA */
#define MAX_PES_HEADER_FREQ     32
#define MAX_PES_PAYLOAD_SIZE    ((MAX_PES_HEADER_FREQ - 1) * 184 + 170)

#define AUDIO_DELTA             (500 * (90000 / 1000))

struct mpegts_stream_t
{
    int               video;
    int               pid;
    int               cc;

    int               payload_index;
    uint64_t          payload_dts;
    uint64_t          payload_pts;
    uint8_t           payload[MAX_PES_PAYLOAD_SIZE];

    int               packets;
};

struct mpegts_muxer_t
{
    int               pcr_pid;   ///< same as video pid
    uint64_t          next_pat;
    int               pat_cc;
    int               pmt_cc;
    int               stream_size;
    mpegts_stream_t * streams;
};

/*++
 *  a simple TS encoder which accepts only one AVC track and one AAC track
 --*/
class TsMuxer
{
public:

    TsMuxer();

    ~TsMuxer();

    bool build(bool video, bool audio);

    void tear();

    void write_header(ts_bucket_t * bucket);

    void write_video_packet(ts_bucket_t * bucket, uint64_t dts, uint64_t pts, unsigned char const * first, unsigned char const * last);

    void write_audio_packet(ts_bucket_t * bucket, uint64_t dts, uint64_t pts, unsigned char const * first, unsigned char const * last);

private:

    void write_pts(uint8_t * q, int fourbits, int64_t pts);

    int convert_to_nal(unsigned char const * first, unsigned char const * last, unsigned char * dst);

    uint32_t get_crc32(uint32_t crc, const uint8_t * buffer, size_t length);

    mpegts_stream_t * mpegts_stream_init(int video, int pid);

    void mpegts_stream_exit(mpegts_stream_t * stream);

    void mpegts_muxer_exit(mpegts_muxer_t * mpegts_muxer);

    mpegts_muxer_t * mpegts_muxer_init(mpegts_stream_t * streams, int stream_size);

    void mpegts_muxer_write_pat(ts_bucket_t * bucket);

    void mpegts_muxer_write_pmt(ts_bucket_t * bucket);

    int packetized_packets(mpegts_stream_t * stream, uint64_t dts, uint64_t pts, unsigned int payload_size);

    void write_packet(mpegts_stream_t * stream, ts_bucket_t * bucket, uint64_t dts, uint64_t pts, unsigned char const * payload, int payload_size);

    void flush_audio_packet(ts_bucket_t * bucket);

private:

    mpegts_muxer_t  * m_muxer;

    mpegts_stream_t * m_astream;

    mpegts_stream_t * m_vstream;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___TSMUXER_H___
