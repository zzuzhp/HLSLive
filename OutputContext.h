#ifndef ___OUTPUTCONTEXT_H___
#define ___OUTPUTCONTEXT_H___

#include "config.h"
#include "IOContext.h"

/////////////////////////////////////////////////////////////////////////////
////

class OutputContext : public IOContext
{
public:

    OutputContext();

    ~OutputContext();

    bool setparam(IOutputContextSink * pfnSink);

    bool addbuffer(unsigned char * buf, int buf_size, unsigned long ts){return false;}

    int read(unsigned char * buf, int buf_size){return -1;}

    int write(unsigned char * buf, int buf_size);

    int64_t seek(int64_t offset, int whence){return -1;}

    friend int write(void * opaque, unsigned char * buf, int buf_size);

private:

    void clean();

private:

    IOutputContextSink * m_pfnSink;
} ;

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___OUTPUTCONTEXT_H___
