#pragma once

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

struct birdcage_used_hdr
{
  /* total size of the allocation, including pre + post padding */
  uintptr_t size;

  /* offset of this used_hdr from the start of where the
   * former free_hdr used to be */
  uintptr_t offset;
};

struct birdcage_bucket
{
  struct birdcage_hdr* first_free;
};

struct birdcage
{
  void* ptr;
  size_t capacity;

  struct birdcage_bucket buckets[BIRDCAGE_NUM_BUCKETS];
};

struct birdcage
birdcage_create(void* buffer, size_t buffer_size);

void*
birdcage_alloc_ex(struct birdcage*, size_t size, size_t alignment);

inline void*
birdcage_alloc(struct birdcage* cage, size_t size)
{
  return birdcage_alloc_ex(cage, size, alignof(max_align_t));
}

#define birdcage_alloc_type(cage, type)                                        \
  birdcage_alloc_ex(cage, sizeof(type), alignof(type))

void
birdcage_free(struct birdcage*, void* ptr);
