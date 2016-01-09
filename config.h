#ifndef ___CONFIG_H___
#define ___CONFIG_H___

/////////////////////////////////////////////////////////////////////////////
////

#define LIVE_CAMERA     1   ///< 1: capture default dshow camera, 0: capture specified file(H264 ES stream)
#define LIVE_CAMERA_IDC 0   ///< specify system camera index
#define LIVE_FILE_NAME  "City_704x576_60_QP24.dec.264"  ///< specify file full path(including file name)
#define NO_RELAY        0   ///< do not relay the stream just mux and dump it.

#define WALLCLOCK_TS    1   ///< parse system time to timestamp
#define OPEN_PREVIEW    1   ///< enable preview before segmention
#define SUPPRESS_LOGS   1   ///< show packets time info in console

#define USE_FFMPEG_PARSER 0 ///< use ffmpeg to read input
#define USE_FFMPEG_MUXER  0 ///< use ffmpeg to write ts

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___CONFIG_H___
