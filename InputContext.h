#ifndef ___INPUTCONTEXT_H___
#define ___INPUTCONTEXT_H___

#include "config.h"
#include "IOContext.h"
#include <pthread.h>

#include <list>

/////////////////////////////////////////////////////////////////////////////
////

struct VideoBuf
{
    int             pos;     ///< read pos
    unsigned long   ts;      ///< ts in 90khz
    int             len;     ///< video len
    unsigned char   data[1]; ///< video buffer
};

class InputContext : public IOContext
{
public:

    InputContext();

    ~InputContext();

    bool addbuffer(unsigned char * buf, int buf_size, unsigned long timestamp);

    int read(unsigned char * buf, int buf_size);

    int write(unsigned char * buf, int buf_size){return -1;}

    int64_t seek(int64_t offset, int whence){return -1;};

    void prepareQuit();

    friend int read(void * opaque, unsigned char * buf, int buf_size);

private:

    void clean();

private:

    pthread_mutex_t           m_mutex;

    pthread_cond_t            m_cond;

    std::list<VideoBuf *>     m_buffers;
} ;

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___INPUTCONTEXT_H___
