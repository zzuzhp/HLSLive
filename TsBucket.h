#ifndef ___TSBUCKET_H___
#define ___TSBUCKET_H___

#include <stdint.h>
#include <memory.h>
#include <cstdlib>
#include <cassert>

struct ts_chain_t;

struct ts_bucket_t
{
    ts_chain_t   ** chain;
    uint64_t        content_length;
    ts_chain_t    * first;
};

struct ts_buf_t
{
    uint8_t       * pos;
    uint8_t       * last;
};

struct ts_chain_t
{
    ts_buf_t      * buf;
    ts_chain_t    * next;
};

static
ts_bucket_t *
bucket_init()
{
    ts_bucket_t * bucket = (ts_bucket_t *)malloc(sizeof(ts_bucket_t));

    bucket->first           = 0;
    bucket->chain           = &bucket->first;
    bucket->content_length  = 0;

    return bucket;
}

static
void
bucket_destroy(ts_bucket_t * bucket)
{

}

static
void
bucket_insert(ts_bucket_t * bucket, void const * buf, uint64_t size)
{
    ts_buf_t * b = (ts_buf_t *)malloc(sizeof(ts_buf_t));
    if (b == NULL)
    {
        return;
    }

    b->pos = (uint8_t *)malloc(size);
    if (b->pos == NULL)
    {
        free(b);
        return;
    }

    memcpy(b->pos, buf, size);

    b->last = b->pos + size;

    if (bucket->first != 0)
    {
        bucket->chain = &(*bucket->chain)->next;
    }

    *bucket->chain = (ts_chain_t *)malloc(sizeof(ts_chain_t));
    if (*bucket->chain == NULL)
    {
        free(b->pos);
        free(b);

        return;
    }

    (*bucket->chain)->buf  = b;
    (*bucket->chain)->next = NULL;

    bucket->content_length += size;
}

static
ts_buf_t *
bucket_retrieve(ts_bucket_t * bucket)
{
    ts_buf_t * first = 0;

    if (bucket->first != 0)
    {
        ts_chain_t * chain = bucket->first;

        bucket->first = bucket->first->next;
        if (!bucket->first)
        {
            bucket->chain = &bucket->first;
        }

        first = chain->buf;
        free(chain);

        bucket->content_length -= (first->last - first->pos);

        assert(bucket->content_length >= 0);
    }

    return first;
}

#endif // ___TSBUCKET_H___
