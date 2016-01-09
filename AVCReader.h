#ifndef ___AVCREADER_H___
#define ___AVCREADER_H___

#include "AVCFrameChecker.h"

/////////////////////////////////////////////////////////////////////////////
////

class AVCReader;

typedef void (AVCReader::* avcdecode)(unsigned char * buffer, int len);

class AVCReader
{
public:

    AVCReader();

    ~AVCReader();

    bool build(bool rfc);

    void tear();

    bool parse(unsigned char * buffer, int len);

    bool getframe(unsigned char ** buffer, int &len, bool &key, bool wait);

private:

    void decodeNalu(unsigned char * buffer, int len);

    void decodeRFC(unsigned char * buffer, int len);

private:

    enum {INBITBUFFER_SIZE = 1920*1080*3};

    unsigned char   * m_inbitBuffer;

    unsigned char   * m_buffer;

    avcdecode         m_decode;

    int               m_frames;

    int               m_frame_au_offset;

    int               m_last_au_offset;

    int               m_total_bytes;

    AVCFrameChecker * m_frameChecker;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___AVCREADER_H___
