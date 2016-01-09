#include "config.h"
#include "HLSLive.h"
#include "LiveFile.h"
#include "LiveCamera.h"
#include "wingetopt.h"

#include <signal.h>

volatile bool g_terminate = false;

void userinterrupt(int)
{
    fprintf(stderr, "user interrupt, exit...\n");
    g_terminate = true;
}

void check_config()
{
    ///< todo: check flags in the config file
}

int main(int argc, char ** argv)
{
    ILive * live = NULL;
    const void * arg = NULL;

    check_config();

#if LIVE_CAMERA
    live = new LiveCamera();
    arg  = (void *)LIVE_CAMERA_IDC;
#else
    live = new LiveFile;
    arg  = LIVE_FILE_NAME;
#endif // LIVE_CAMERA

    if (!live->start(arg))
    {
        return -1;
    }

    ::signal(SIGINT, &userinterrupt);

    while (!g_terminate) Sleep(1);

    live->stop();

    delete live;

    system("pause");

    return 0;
}
