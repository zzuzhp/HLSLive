#include "TsMuxer.h"
#include "TsIO.h"

#define START_PID               0x100
#define PMT_PID                 0x1000
#define SERVICE_ID              0x0001
#define TS_PACKET_SIZE          188
#define NOPTS_VALUE      INT64_C(0x8000000000000000)

///< resend PAT/PMT every 100ms
#define PAT_DELTA (100 * (90000 / 1000))
///< #define PAT_DELTA               (60 * 1000 * (90000 / 1000))

static uint32_t const crc32[256] =
{
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
    0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
    0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
    0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
    0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
    0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
    0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
    0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
    0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
    0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
    0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
    0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
    0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
    0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
    0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
    0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
    0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
    0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
    0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
    0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
    0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
    0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
    0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
    0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
    0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
    0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
    0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
    0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
    0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
    0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
    0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
    0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
    0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
    0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
    0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
    0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
    0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
    0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
    0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
    0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
    0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
    0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
    0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
    0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
    0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
    0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
    0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
    0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
    0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

TsMuxer::TsMuxer() : m_muxer(NULL),
                     m_astream(NULL),
                     m_vstream(NULL)
{

}

TsMuxer::~TsMuxer()
{
    tear();
}

bool
TsMuxer::build(bool video, bool audio)
{
    mpegts_stream_t * streams[2] = {0};

    int pid    = START_PID;
    int tracks = 0;

    if (!video && !audio)
    {
        return false;
    }

    if (video)
    {
        streams[tracks++] = m_vstream = mpegts_stream_init(1, pid++);
    }

    if (audio)
    {
        streams[tracks++] = m_astream = mpegts_stream_init(1, pid);
    }

    m_muxer = mpegts_muxer_init(*streams, tracks);

    m_muxer->pcr_pid = streams[0]->pid;

    return true;
}

void
TsMuxer::tear()
{
    if (m_vstream)
    {
        mpegts_stream_exit(m_vstream);
        m_vstream = NULL;
    }

    if (m_astream)
    {
        mpegts_stream_exit(m_astream);
        m_astream = NULL;
    }

    if (m_muxer)
    {
        mpegts_muxer_exit(m_muxer);
        m_muxer = NULL;
    }
}

void
TsMuxer::write_header(ts_bucket_t * bucket)
{
    mpegts_muxer_write_pat(bucket);
    mpegts_muxer_write_pmt(bucket);
}

void
TsMuxer::write_video_packet(ts_bucket_t * bucket, uint64_t dts, uint64_t pts, unsigned char const * first, unsigned char const * last)
{
    unsigned char * p, * buf;

    static const unsigned char aud_nal[6] =
    {
        0x00, 0x00, 0x00, 0x01, 0x09, 0xe0
    };

    int size = last - first + sizeof(aud_nal);

    uint8_t firstNaluType;

    if (read_32(first) == 0x00000001)
    {
        firstNaluType = *(first + 4);
    }
    else if(read_24(first) == 0x000001)
    {
        firstNaluType = *(first + 3);
    }
    else
    {
        /* not a valid AVC NALU */
        return;
    }

    buf = (unsigned char *)malloc(size + 10);
    if (buf == NULL)
    {
        return;
    }

    p = buf;

    /* make sure every AU starts with a Delimiter NALU */
    if (firstNaluType & 0x1f != 9)
    {
        /* add a Delimiter NALU */
        memcpy(p, aud_nal, sizeof(aud_nal));
        p += sizeof(aud_nal);
    }

    if (convert_to_nal(first, last, p))
    {
        write_packet(m_vstream, bucket, dts, pts, buf, size);
    }

    free(buf);
}

void
TsMuxer::write_audio_packet(ts_bucket_t * bucket, uint64_t dts, uint64_t pts, unsigned char const * first, unsigned char const * last)
{
    while (first != last)
    {
        unsigned int size = MAX_PES_PAYLOAD_SIZE - m_astream->payload_index;
        int flush = 0;

        if (size > (unsigned int)(last - first))
        {
            size = last - first;
        }

        memcpy(m_astream->payload + m_astream->payload_index, first, size);

        first += size;
        m_astream->payload_index += size;

        if (m_astream->payload_index == MAX_PES_PAYLOAD_SIZE)
        {
            flush = 1;
        }

        if (m_astream->payload_dts != NOPTS_VALUE &&
            dts != NOPTS_VALUE && dts - m_astream->payload_dts >= AUDIO_DELTA)
        {
            flush = 1;
        }

        if (flush)
        {
            flush_audio_packet(bucket);
        }
    }
}

void
TsMuxer::write_packet(mpegts_stream_t * stream, ts_bucket_t * bucket, uint64_t dts, uint64_t pts, unsigned char const * payload, int payload_size)
{
    unsigned char * buf;
    unsigned char * q;

    int write_discontinuity_indicator = stream->packets == 0; ///<!

    int is_start = 1;

    ///< const int max_delay = 90000 / 25;
    ///< const int max_delay = 90000 / 2;
    const int max_delay = 0;/// 90000;

    if (dts != NOPTS_VALUE)
    {
        dts += max_delay;
    }

    if (pts != NOPTS_VALUE)
    {
        pts += max_delay;
    }

#if 1 ///< PAT/PMT is manually inserted by 'write_header()'
    /* automatically insert PAT/PMT */
    if (m_muxer->next_pat == NOPTS_VALUE || (dts != NOPTS_VALUE && dts >= m_muxer->next_pat))
    {
        mpegts_muxer_write_pat(bucket);

        mpegts_muxer_write_pmt(bucket);

        m_muxer->next_pat = dts + PAT_DELTA;
    }
#endif // 0

    /* reserve the exact number of packets we need for this payload */
    int packets = packetized_packets(stream, dts, pts, payload_size);

    size_t out_size = packets * TS_PACKET_SIZE;

    unsigned char * out_buf = (unsigned char *)malloc(out_size);
    if (out_buf == NULL)
    {
        return;
    }

    buf = out_buf;

    while (payload_size)
    {
        int write_pcr = is_start && stream->pid == m_muxer->pcr_pid;
        int val;

        /* prepare packet header */
        q = buf;

        /* sync byte */
        *q++ = 0x47;

        val = stream->pid >> 8;
        if (is_start)
        {
            /* payload unit start indicator */
            val |= 0x40;
        }

        /* three one-bit flags */
        *q++ = val;

        /* 13-bit Packet Identifier */
        *q++ = stream->pid;

        /* 4-bit continuity counter */
        *q++ = 0x10 | stream->cc | ((write_pcr || write_discontinuity_indicator) ? 0x20 : 0);
        stream->cc = (stream->cc + 1) & 0xf;

        if (write_pcr)
        {
            int64_t pcr = dts - max_delay + 1;

            /* Adaptation Field Length */
            *q++ = 7;

            /* Adaptation field contains a PCR field */
            *q++ = 0x10;

            /* Program clock reference */
            *q++ = (unsigned char)(pcr >> 25);
            *q++ = (unsigned char)(pcr >> 17);
            *q++ = (unsigned char)(pcr >> 9);
            *q++ = (unsigned char)(pcr >> 1);
            *q++ = (unsigned char)((pcr & 1) << 7);
            *q++ = 0;
        }
        else if (write_discontinuity_indicator)
        {
            /* Adaptation Field Length */
            *q++ = 1;
            *q++ = 0x80;

            write_discontinuity_indicator = 0;
        }

        if (write_discontinuity_indicator)
        {
            buf[5] |= 0x80;

            write_discontinuity_indicator = 0;
        }

        /* random access indicator */
        if (stream->packets == 0)
        {
            buf[5] |= 0x40; ///< for video: check whether this is a key frame ?
        }

        if (is_start)
        {
            /* PES headers, See: http://www.mpucoder.com/DVD/pes-hdr.html */
            int header_len = 0;
            int flags      = 0;

            int len;

            /* PES header start code */
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x01;

            /* stream id */
            if (stream->video)
            {
                *q++ = 0xe0;
            }
            else
            {
                *q++ = 0xbd;  ///< private_stream_1 (for AAC)
            }

            /* PTS */
            if (pts != NOPTS_VALUE)
            {
                header_len += 5;
                flags |= 0x80;

                if (dts != NOPTS_VALUE && dts != pts)
                {
                    /* DTS */
                    header_len += 5;
                    flags |= 0x40;
                }
            }

            len = payload_size + header_len + 3;

            if (len > 0xffff)
            {
                len = 0;
            }

            /* packet length */
            *q++ = len >> 8;
            *q++ = len;

            *q++ = 0x80;
            *q++ = flags;
            *q++ = header_len;

            if (flags & 0x80)
            {
                /* pts */
                write_pts(q, flags >> 6, pts);
                q += 5;
            }

            if (flags & 0x40)
            {
                /* dts */
                write_pts(q, 1, dts);
                q += 5;
            }

            is_start = 0;
        }

        /* stuffing & PES payload */
        {
            /* header size */
            int header_len = q - buf;

            /* data length */
            int data_len = TS_PACKET_SIZE - header_len;
            int stuffing_len = data_len;

            if (data_len > payload_size)
            {
                /* needs stuffing */
                data_len = payload_size;
            }

            stuffing_len -= data_len;

            if (stuffing_len > 0)
            {
                /* add stuffing */
                if (buf[3] & 0x20)
                {
                    /* stuffing with AFC (Adaptation Field Control) */
                    int afc_len = buf[4] + 1/* 1: the adaptation_field_length(8 bit) */;

                    /* PES header */
                    memmove(buf + 4 + afc_len + stuffing_len, buf + 4 + afc_len, header_len - (4 + afc_len));

                    /* increase size of AF */
                    buf[4] += stuffing_len;

                    /* stuffing */
                    memset(buf + 4 + afc_len, 0xff, stuffing_len);
                }
                else
                {
                    memmove(buf + 4 + stuffing_len, buf + 4, header_len - 4);

                    /* enable AFC */
                    buf[3] |= 0x20;
                    buf[4]  = stuffing_len - 1;

                    if (stuffing_len >= 2)
                    {
                        buf[5] = 0x00;
                        memset(buf + 6, 0xff, stuffing_len - 2);
                    }
                }
            }

#if 0
            if (write_discontinuity_indicator && (buf[3] & 0x20))
            {
                buf[5] |= 0x80;
                write_discontinuity_indicator = 0;
            }
#endif

            memcpy(buf + TS_PACKET_SIZE - data_len, payload, data_len);

            payload      += data_len;
            payload_size -= data_len;

            ++stream->packets;
        }

        buf += TS_PACKET_SIZE;
    }

    if (buf != (unsigned char const *)out_buf + out_size)
    {
        ///< printf("write_packet: incorrect number of packets.\n");
    }

    bucket_insert(bucket, out_buf, out_size);

    free(out_buf);
}

void
TsMuxer::flush_audio_packet(ts_bucket_t * bucket)
{
    if (m_astream->payload_index)
    {
        write_packet(m_astream,
                     bucket,
                     m_astream->payload_dts,
                     m_astream->payload_pts,
                     m_astream->payload,
                     m_astream->payload_index);

        m_astream->payload_index    = 0;
        m_astream->payload_dts      = NOPTS_VALUE;
        m_astream->payload_pts      = NOPTS_VALUE;
    }
}

mpegts_stream_t *
TsMuxer::mpegts_stream_init(int video, int pid)
{
    mpegts_stream_t * stream = (mpegts_stream_t *)malloc(sizeof(mpegts_stream_t));

    stream->video            = video;
    stream->pid              = pid;
    stream->cc               = 0;
    stream->payload_index    = 0;
    stream->payload_dts      = NOPTS_VALUE;
    stream->payload_pts      = NOPTS_VALUE;
    stream->packets          = 0;

    return stream;
}

void
TsMuxer::mpegts_stream_exit(mpegts_stream_t * stream)
{
    free(stream);
}

mpegts_muxer_t *
TsMuxer::mpegts_muxer_init(mpegts_stream_t * streams, int stream_size)
{
    mpegts_muxer_t * muxer = (mpegts_muxer_t *)malloc(sizeof(mpegts_muxer_t));

    muxer->pcr_pid      = 0x1fff;
    muxer->next_pat     = NOPTS_VALUE;
    muxer->pat_cc       = 0;
    muxer->pmt_cc       = 0;
    muxer->stream_size  = stream_size;
    muxer->streams      = streams;

    return muxer;
}

void
TsMuxer::mpegts_muxer_exit(mpegts_muxer_t * mpegts_muxer)
{
    free(mpegts_muxer);
}

/* Program Association Table lists all the programs available in the transport stream. */
void
TsMuxer::mpegts_muxer_write_pat(ts_bucket_t * bucket)
{
    unsigned char packet[TS_PACKET_SIZE];
    uint8_t * q = packet;
    uint8_t * section_start;
    uint8_t * section_end;

    const int section_payload_len           = 4;
    unsigned int crc                        = -1;
    const int PAT_PID                       = 0x0000;
    const int pat_table_id                  = 0x00;
    const int default_transport_stream_id   = 0x0001;

    /* packet header */
    q = write_8(q, 0x47);
    q = write_16(q, 0x4000 | PAT_PID);
#if 0
    q = write_8(q, 0x10 | m_muxer->pat_cc);
#else
    q = write_8(q, 0x30 | m_muxer->pat_cc);
    q = write_8(q, 1);
    q = write_8(q, 0x80);
#endif
    m_muxer->pat_cc = (m_muxer->pat_cc + 1) & 0xf;
    q = write_8(q, 0);

    /* section header */
    section_start = q;
    q = write_8(q, pat_table_id);
    /* 5 byte header + 4 byte CRC */
    q = write_16(q, 0xb000 | (section_payload_len + 5 + 4));
    /* transport stream id */
    q = write_16(q, default_transport_stream_id);

    *q++ = 0xc1;
    *q++ = 0x00;
    *q++ = 0x00;

    /* section payload */
    q = write_16(q, SERVICE_ID);
    q = write_16(q, 0xe000 | PMT_PID);

    section_end = q;

    /* crc */
    crc = get_crc32(crc, section_start, section_end - section_start);
    q   = write_32(q, crc);

    memset(q, 0xff, packet + TS_PACKET_SIZE - q);

    bucket_insert(bucket, packet, TS_PACKET_SIZE);
}

/* Program Map Tables contain information about programs. */
void
TsMuxer::mpegts_muxer_write_pmt(ts_bucket_t * bucket)
{
    unsigned char packet[TS_PACKET_SIZE];
    uint8_t * q = packet;
    uint8_t * section_start;
    uint8_t * section_end;

    const int pmt_table_id = 0x02;
    unsigned int crc = -1;

    int section_payload_len = 4;
    section_payload_len += m_muxer->stream_size * 5;

    /* packet header */
    q = write_8(q, 0x47);
    q = write_16(q, 0x4000 | PMT_PID);
#if 0
    q = write_8(q, 0x10 | m_muxer->pmt_cc);
#else
    q = write_8(q, 0x30 | m_muxer->pmt_cc);
    q = write_8(q, 1);
    q = write_8(q, 0x80);
#endif

    m_muxer->pmt_cc = (m_muxer->pmt_cc + 1) & 0xf;
    q = write_8(q, 0);

    // section header
    section_start = q;
    q = write_8(q, pmt_table_id);
    // 5 byte header + 4 byte CRC
    q = write_16(q, 0xb000 | (section_payload_len + 5 + 4));
    // service identifier
    q = write_16(q, SERVICE_ID);

    *q++ = 0xc1;
    *q++ = 0x00;
    *q++ = 0x00;

    // section payload
    q = write_16(q, 0xe000 | m_muxer->pcr_pid);
    q = write_16(q, 0xf000);

    int i = 0;
    for(; i < m_muxer->stream_size; ++i)
    {
        if(m_muxer->streams[i].video)
        {
          q = write_8(q, 0x1b);
        }
        else
        {
            q = write_8(q, 0x0f);
        }

        q = write_16(q, 0xe000 | m_muxer->streams[i].pid);
        q = write_16(q, 0xf000);
    }

    section_end = q;

    // crc
    crc = get_crc32(crc, section_start, section_end - section_start);
    q   = write_32(q, crc);

    memset(q, 0xff, packet + TS_PACKET_SIZE - q);

    bucket_insert(bucket, packet, TS_PACKET_SIZE);
}

int
TsMuxer::packetized_packets(mpegts_stream_t * stream, uint64_t dts, uint64_t pts, unsigned int payload_size)
{
    int write_discontinuity_indicator = stream->packets == 0; ///<!

    /* calculate overhead: AF & PES header */
    int once = 0;

    if (stream->pid == m_muxer->pcr_pid)
    {
        once += 8; ///< AF length
    }
    else if (write_discontinuity_indicator)
    {
        once += 2; ///< AF length
    }

    /* PES header start code, stream id */
    once += 4;

    if (pts != NOPTS_VALUE)
    {
        once += 5;

        if (dts != NOPTS_VALUE && dts != pts)
        {
            once += 5;
        }
    }

    /* packet length, 0x80, flags, header length */
    once += 5;

    if (payload_size <= 184 - once)
    {
        return 1;
    }

    return 1 + ((payload_size - (184 - once) + 184 - 1) / 184);
}

void
TsMuxer::write_pts(uint8_t * q, int fourbits, int64_t pts)
{
    int val = val = fourbits << 4 | (((pts >> 30) & 0x07) << 1) | 1;

    *q++ = val;

    val = (((pts >> 15) & 0x7fff) << 1) | 1;

    *q++ = val >> 8;
    *q++ = val;

    val = (((pts) & 0x7fff) << 1) | 1;

    *q++ = val >> 8;
    *q++ = val;
}

int
TsMuxer::convert_to_nal(unsigned char const * first, unsigned char const * last, unsigned char * dst)
{
#if 1
    /* check if data is already in nal format. Shouldn't be necessary and this */
    /* is only a hack for Live Smooth Streaming */
    if (read_32(first) == 0x00000001 || read_24(first) == 0x000001)
    {
        memcpy(dst, first, last - first);
        return 1;
    }
#endif

    while(first < last)
    {
        uint32_t packet_len = read_32(first);
        if (packet_len > (uint32_t)(last - first))
        {
            return 0;
        }

        first += 4;

        write_32(dst, 0x00000001);
        dst += 4;

        memcpy(dst, first, packet_len);

        first += packet_len;
        dst   += packet_len;
    }

    return 1;
}

uint32_t
TsMuxer::get_crc32(uint32_t crc, const uint8_t * buffer, size_t length)
{
    uint8_t const * first = buffer;
    uint8_t const * last  = buffer + length;

    while(first != last)
    {
        crc = (crc << 8) ^ crc32[(crc >> 24) ^ (uint32_t)(*first++)];
    }

    return crc;
}
