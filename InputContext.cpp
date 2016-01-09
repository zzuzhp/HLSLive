#include "InputContext.h"
#include <cassert>

/////////////////////////////////////////////////////////////////////////////////
////

int
g_read(void * opaque, unsigned char * buf, int buf_size)
{
    InputContext * inst = static_cast<InputContext *>(opaque);
    return inst->read(buf, buf_size);
}

InputContext::InputContext()
{
    pthread_mutex_init(&m_mutex, NULL);

    pthread_cond_init(&m_cond, NULL);

    m_ctx = avio_alloc_context((unsigned char *)m_buffer,
                               AVIO_BUFFER_SIZE,
                               0,
                               this,
                               &g_read,
                               NULL,
                               NULL);
}

InputContext::~InputContext()
{
    clean();
}

void
InputContext::clean()
{
    prepareQuit();

    pthread_mutex_lock(&m_mutex);

    if (m_ctx)
    {
        /* note: the internal buffer could have changed internally by ffmpeg. */
        ::av_freep(&m_ctx->buffer);
        m_buffer = NULL;

        ::av_freep(m_ctx);
        m_ctx = NULL;
    }

    while (!m_buffers.empty())
    {
        VideoBuf * buf = m_buffers.front();
        m_buffers.pop_front();

        delete[] (unsigned char *)buf;
    }

    pthread_mutex_unlock(&m_mutex);

    pthread_cond_destroy(&m_cond);

    pthread_mutex_destroy(&m_mutex);
}

void
InputContext::prepareQuit()
{
    IOContext::prepareQuit();

    pthread_cond_signal(&m_cond);
}

bool
InputContext::addbuffer(unsigned char * buf, int buf_size, unsigned long ts)
{
    VideoBuf * vbuf = NULL;

    if (!m_buffer || !m_ctx || m_exit)
    {
        return false;
    }

    if (buf_size > 0)
    {
        unsigned char * mem = new unsigned char[sizeof(VideoBuf) + buf_size - 1];

        assert(mem);
        if (!mem)
        {
            return false;
        }

        vbuf = (VideoBuf *)mem;

        vbuf->pos = 0;
        vbuf->ts  = ts;
        vbuf->len = buf_size;

        memcpy(vbuf->data, buf, buf_size);

        pthread_mutex_lock(&m_mutex);

        m_buffers.push_back(vbuf);

        pthread_cond_signal(&m_cond);

        pthread_mutex_unlock(&m_mutex);
    }

    return true;
}

int
InputContext::read(unsigned char * buf, int buf_size)
{
    int readlen = 0;

    if (m_exit)
    {
        return -1;
    }

    pthread_mutex_lock(&m_mutex);

    while (readlen < buf_size)
    {
        if (m_buffers.empty())
        {
            pthread_cond_wait(&m_cond, &m_mutex);
        }

        if (m_exit)
        {
            break;
        }

        VideoBuf * vbuf = m_buffers.front();

        int cpylen = FFMIN(vbuf->len - vbuf->pos, buf_size - readlen);

        memcpy(buf + readlen, &vbuf->data[vbuf->pos], cpylen);

        readlen   += cpylen;
        vbuf->pos += cpylen;

        if (vbuf->pos == vbuf->len)
        {
            m_buffers.pop_front();

            delete[] (unsigned char *)vbuf;
        }
    }

    pthread_mutex_unlock(&m_mutex);

    return readlen;
}

/////////////////////////////////////////////////////////////////////////////////
////
