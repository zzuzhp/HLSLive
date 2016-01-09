#ifndef ___TSIO_H___
#define ___TSIO_H___

#include <stdint.h>

/////////////////////////////////////////////////////////////////////////////
////

static unsigned int read_8(unsigned char const * buffer)
{
    return buffer[0];
}

static unsigned char * write_8(unsigned char * buffer, unsigned int v)
{
    buffer[0] = (uint8_t)v;
    return buffer + 1;
}

static uint16_t read_16(unsigned char const * buffer)
{
    return (buffer[0] << 8) | (buffer[1] << 0);
}

static unsigned char * write_16(unsigned char * buffer, unsigned int v)
{
    buffer[0] = (uint8_t)(v >> 8);
    buffer[1] = (uint8_t)(v >> 0);

    return buffer + 2;
}

static unsigned int read_24(unsigned char const * buffer)
{
    return (buffer[0] << 16) | (buffer[1] << 8) | (buffer[2] << 0);
}

static uint32_t read_32(unsigned char const * buffer)
{
    return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | (buffer[3] << 0);
}

static unsigned char * write_32(unsigned char * buffer, uint32_t v)
{
    buffer[0] = (uint8_t)(v >> 24);
    buffer[1] = (uint8_t)(v >> 16);
    buffer[2] = (uint8_t)(v >> 8);
    buffer[3] = (uint8_t)(v >> 0);

    return buffer + 4;
}

static uint64_t read_64(unsigned char const * buffer)
{
    return ((uint64_t)(read_32(buffer)) << 32) + read_32(buffer + 4);
}

static unsigned char * write_64(unsigned char * buffer, uint64_t v)
{
    write_32(buffer + 0, (uint32_t)(v >> 32));
    write_32(buffer + 4, (uint32_t)(v >> 0));

    return buffer + 8;
}

/////////////////////////////////////////////////////////////////////////////
////

#endif // ___TSIO_H___
