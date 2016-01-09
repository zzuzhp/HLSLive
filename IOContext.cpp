#include "IOContext.h"
#include "InputContext.h"
#include "OutputContext.h"

IOContext *
createIOContext(bool input, void * arg)
{
    if (input)
    {
        InputContext * ictx = new InputContext();
        if (!ictx)
        {
            return NULL;
        }

        return ictx;
    }
    else
    {
        OutputContext * octx = new OutputContext();
        if (!octx)
        {
            return NULL;
        }

        if (!octx->setparam((IOutputContextSink *) arg))
        {
            delete octx;
            return NULL;
        }

        return octx;
    }

    return 0; ///< unreachable
}

void
destroyIOContext(IOContext * ctx)
{
    if (ctx)
    {
        delete ctx;
    }
}
