#include "OutputContext.h"
#include <cassert>

/////////////////////////////////////////////////////////////////////////////////
////

int
g_write(void * opaque, unsigned char * buf, int buf_size)
{
    OutputContext * inst = static_cast<OutputContext *>(opaque);
    return inst->write(buf, buf_size);
}

OutputContext::OutputContext() : m_pfnSink(NULL)
{

}

OutputContext::~OutputContext()
{
    clean();
}

void
OutputContext::clean()
{
    if (m_ctx)
    {
        /* note: the internal buffer could have changed internally by ffmpeg. */
        ::av_freep(&m_ctx->buffer);
        m_buffer = NULL;

        ::av_freep(m_ctx);
        m_ctx = NULL;
    }
}

bool
OutputContext::setparam(IOutputContextSink * pfnSink)
{
    if (!m_buffer)
    {
        return false;
    }

    m_ctx = avio_alloc_context((unsigned char *)m_buffer,
                               AVIO_BUFFER_SIZE,
                               1,
                               this,
                               NULL,
                               &g_write,
                               NULL);

    assert(m_ctx);
    if (!m_ctx)
    {
        return false;
    }

    m_pfnSink = pfnSink;

    return true;
}

int
OutputContext::write(unsigned char * buf, int buf_size)
{
    if (m_pfnSink)
    {
        (m_pfnSink->OCtxOutput)(buf, buf_size);
        return buf_size;
    }

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////
////
