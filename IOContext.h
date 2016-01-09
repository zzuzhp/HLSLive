#ifndef ___IOCONTEXT_H___
#define ___IOCONTEXT_H___

extern "C"
{
#include <libavformat/avio.h>
}

#include <cassert>

class IOutputContextSink
{
public:
    virtual void OCtxOutput(uint8_t * data, int32_t len) = 0;
};

/////////////////////////////////////////////////////////////////////////////
////

class IOContext
{
public:

    IOContext() : m_exit(false),
                  m_ctx(NULL),
                  m_buffer(NULL)
    {
        m_buffer = static_cast<unsigned char *>(::av_malloc(AVIO_BUFFER_SIZE));
    }

    virtual ~IOContext()
    {
        ///< 'm_buffer' should have been freed in derived classes.
        assert(!m_buffer);
    }

    virtual bool addbuffer(unsigned char * buf, int buf_size, unsigned long ts) = 0;

    virtual int read(unsigned char * buf, int buf_size) = 0;

    virtual int write(unsigned char * buf, int buf_size) = 0;

    virtual int64_t seek(int64_t offset, int whence) = 0;

    virtual void prepareQuit() {m_exit = true;}

    operator AVIOContext *() {return m_ctx;}

protected:

    enum {AVIO_BUFFER_SIZE = 4096}; ///< a default 4k buffer size

    volatile bool     m_exit;       ///< signal to stop feeding bytes

    AVIOContext     * m_ctx;        ///< the AVIOContext

    unsigned char   * m_buffer;     ///< AVIOContext buffer
};

/**
  * @param arg is ignored for InputContext, OCtxOutput for OutputContext.
  */
IOContext *
createIOContext(bool input, void * arg = 0);

void
destroyIOContext(IOContext * ctx);

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___IOCONTEXT_H___
