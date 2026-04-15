#pragma once

/*
Copyright 2026 corvid3@github

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

enum : unsigned
{
  /* buckets start with a power of 5 (smallest allocation is 32 bytes) */
  BIRDCAGE_BUCKET_START = 5,

  /* buckets start with a power of 14 (all regions > 16k are grouped) */
  BIRDCAGE_BUCKET_END = 14,
  BIRDCAGE_NUM_BUCKETS = BIRDCAGE_BUCKET_END - BIRDCAGE_BUCKET_START,
};

struct birdcage_hdr
{
  struct birdcage_hdr* next;
  uintptr_t size;
  struct birdcage_hdr* prev;

  /* currently unused, future plans (coalesce behind the current block?) */
  uintptr_t unused;
};

struct birdcage_bucket
{
  struct birdcage_hdr* first_free;
};

struct birdcage
{
  void* ptr;
  size_t capacity;
  size_t usage;

  struct birdcage_bucket buckets[BIRDCAGE_NUM_BUCKETS];
};

#define birdcage_used(cage) ((cage).usage)
#define birdcage_unused(cage) ((cage).capacity - birdcage_used(cage))

struct birdcage
birdcage_create(void* buffer, size_t buffer_size);

void*
birdcage_alloc_ex(struct birdcage*, size_t size, size_t alignment);

void*
birdcage_realloc_ex(struct birdcage* restrict cage,
                    void* ptr,
                    size_t newsize,
                    size_t newalignment);

inline void*
birdcage_realloc(struct birdcage* restrict cage,
                 void* restrict const ptr,
                 size_t const size)
{
  return birdcage_realloc_ex(cage, ptr, size, alignof(max_align_t));
}

inline void*
birdcage_alloc(struct birdcage* cage, size_t size)
{
  return birdcage_alloc_ex(cage, size, alignof(max_align_t));
}

#define birdcage_alloc_type(cage, type)                                        \
  birdcage_alloc_ex(cage, sizeof(type), alignof(type))

void
birdcage_free(struct birdcage*, void* ptr);
