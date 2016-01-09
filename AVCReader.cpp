#include "AVCReader.h"

#include <memory.h>
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <string>

static const int  startcodelen = 4;
static const char avcstartcode[startcodelen] = {'\0', '\0', '\0', '\1'};

#define AVCCOPY(x, y) \
            if (m_buffer - m_inbitBuffer + (y) > INBITBUFFER_SIZE) \
            {\
                assert(0); \
                return; \
            }\
            \
            memcpy(m_buffer, (x), (y)); \
            m_buffer += (y);

#define ADD_STARTCODE AVCCOPY(avcstartcode, startcodelen)

#define ADD_NALUHEADER(x) \
            ADD_STARTCODE \
			*m_buffer++ = (x); ///! buffer size

#define FLUSH_DECODE \
            decodeNalu(m_inbitBuffer, m_buffer - m_inbitBuffer); \
            m_buffer = m_inbitBuffer;

//////////////////////////////////////////////////////////////////////////
////

AVCReader::AVCReader() : m_inbitBuffer(NULL),
                         m_buffer(NULL),
                         m_decode(NULL),
                         m_frames(0),
                         m_frame_au_offset(0),
                         m_last_au_offset(0),
                         m_total_bytes(0),
                         m_frameChecker(NULL)
{

}

AVCReader::~AVCReader()
{
    tear();
}

bool
AVCReader::build(bool rfc)
{
    m_inbitBuffer = new unsigned char[INBITBUFFER_SIZE];

    assert(m_inbitBuffer);
    if (!m_inbitBuffer)
    {
        goto fail;
    }

    m_frameChecker = new AVCFrameChecker();

    assert(m_frameChecker);
    if (!m_frameChecker)
    {
        goto fail;
    }

    m_decode = rfc ? &AVCReader::decodeRFC : &AVCReader::decodeNalu;

    return true;

fail:

    tear();

    return false;
}

void
AVCReader::tear()
{
    if (m_frameChecker)
    {
        delete m_frameChecker;
        m_frameChecker = NULL;
    }

    if (m_inbitBuffer)
    {
        delete[] m_inbitBuffer;
        m_inbitBuffer = NULL;
    }

    m_frame_au_offset   = 0;
    m_last_au_offset    = 0;
    m_total_bytes       = 0;
    m_buffer            = NULL;
    m_frames            = 0;
    m_decode            = NULL;
}

bool
AVCReader::parse(unsigned char * buffer, int len)
{
    (this->*m_decode)(buffer, len);

    return true;
}

bool
AVCReader::getframe(unsigned char ** buffer, int &len, bool &key, bool wait)
{
    H264AU * au = NULL;
    unsigned char * data = NULL;

    len = 0;

    do
    {
       au = m_frameChecker->getAU();
       if (au)
       {
           std::list<H264NALU *>::iterator itr = au->nalus.begin();
           for (; itr != au->nalus.end(); ++itr)
           {
               H264NALU * nalu = *itr;
               len += nalu->len;
           }

           *buffer = data = new unsigned char[len];

           assert(data);
           if (!data)
           {
               m_frameChecker->freeAU(au);
               au = NULL;

               break;
           }

           for (itr = au->nalus.begin(); itr != au->nalus.end(); ++itr)
           {
               H264NALU * nalu = *itr;

               memcpy(data, nalu->data, nalu->len);
               data += nalu->len;
           }

           key = au->key;

           m_frameChecker->freeAU(au);

           break;
       }

       Sleep(1);
    } while (!au && wait);

    return !!au;
}

void
AVCReader::decodeNalu(unsigned char * buffer, int len)
{
    m_total_bytes += len;

    do
    {
        int consumed = m_frameChecker->addBytes(buffer, len, m_frame_au_offset);

        buffer += consumed;
        len    -= consumed;

        m_frame_au_offset += consumed;

        if (m_frameChecker->frameStart())
        {
            bool keyframe       = m_frameChecker->keyFrameStart();
            int  cur_au_offset  = m_frameChecker->frameAUstreamOffset();

            if (m_last_au_offset != -1)
            {
//                fprintf(file2, "%d\n", cur_au_offset - m_last_au_offset);
//                fflush(file2);
            }

            m_last_au_offset = cur_au_offset;

            ++m_frames;
        }
    } while (len);
}

void
AVCReader::decodeRFC(unsigned char * buffer, int len)
{
    if (len <= 0 || buffer == 0)
    {
        return;
    }

    uint8_t type = *buffer & 0x1f;
    switch (type)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12: ///< single NALU packet
        {
            ADD_STARTCODE

            AVCCOPY(buffer, len)

            FLUSH_DECODE

            break;
        }

    case 24: ///< STAPA
        {
            ++buffer;
            --len;

            do
            {
                uint8_t lenHi = *(uint8_t *)buffer++;
                uint8_t lenLo = *(uint8_t *)buffer++;

                uint16_t naluLen = (lenHi << 8) | lenLo;

                len -= 2;

                if (naluLen > len)
                {
                    return;
                }

                ADD_STARTCODE

                AVCCOPY(buffer, naluLen)

                buffer += naluLen;
                len    -= naluLen;

                FLUSH_DECODE
            } while (len);

            break;
        }
    case 28:
        {
            ///< FUA
            uint8_t temp        = *buffer++ & 0xe0;
            uint8_t naluHeader  = temp | (*buffer & 0x1f);
            bool start          = (*buffer & 0x80) > 0;
            bool end            = (*buffer++ & 0x40) > 0;

            len -= 2;

            if (len < 0 || (start && end))
            {
                assert(0);
                break;
            }

            if (start)
            {
                ADD_NALUHEADER(naluHeader)
            }

            AVCCOPY(buffer, len);

            if (end)
            {
                FLUSH_DECODE
            }

            break;
        }
    default:
        {
            ///< Not Supported!
            assert(0);
            break;
        }
    }
}
