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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "birdcage.h"

#define MAX(x, y) ((x > y) ? x : y)
#define MIN(x, y) ((x < y) ? x : y)

enum
{
  /* also works as the minimum alignment */
  HDR_ALIGNMENT = 4,

  MIN_BLOCK_SIZE = (1U << BIRDCAGE_BUCKET_START),
};

static size_t
clamp(size_t const in, size_t const max, size_t const min)
{
  if (in < min)
    return min;
  if (in > max)
    return max;
  return in;
}

static inline unsigned
blog2(unsigned const in)
{
  assert(in != 0);

  enum
  {
    bits_per_byte = 8,
  };

  return (sizeof(unsigned) * bits_per_byte) - __builtin_clz(in) - 1;
}

static unsigned
bucket_idx_for_size(size_t const size)
{
  return clamp(blog2(size), BIRDCAGE_BUCKET_END - 1, BIRDCAGE_BUCKET_START) -
         BIRDCAGE_BUCKET_START;
}

static unsigned
bucket_idx_for_hdr(struct birdcage_hdr const* restrict hdr)
{
  return bucket_idx_for_size(hdr->size);
}

static struct birdcage_bucket*
hdrs_bucket(struct birdcage* restrict cage, struct birdcage_hdr* restrict hdr)
{
  return &cage->buckets[bucket_idx_for_hdr(hdr)];
}

static void
add_to_list(struct birdcage_bucket* bucket, struct birdcage_hdr* hdr)
{
  if (bucket->first_free == hdr)
    fprintf(stderr, "DOUBLE-FREE DETECTED!\n"), abort();

  if (bucket->first_free)
    bucket->first_free->prev = hdr;
  hdr->next = bucket->first_free;
  bucket->first_free = hdr;
  hdr->prev = 0;
}

static void
remove_from_list(struct birdcage_bucket* bucket, struct birdcage_hdr* hdr)
{
  struct birdcage_hdr* const hind = hdr->prev;

  if (hind)
    hind->next = hdr->next;
  else
    bucket->first_free = hdr->next;
  if (hdr->next)
    hdr->next->prev = hind;

  hdr->prev = 0;
  hdr->next = 0;
}

static void
insert(struct birdcage* restrict cage, struct birdcage_hdr* hdr)
{
  add_to_list(hdrs_bucket(cage, hdr), hdr);
}

static bool
is_used(struct birdcage_hdr const* restrict hdr)
{
  return (uintptr_t)hdr->next & 1U;
}

static bool
is_free(struct birdcage_hdr const* restrict hdr)
{
  return !is_used(hdr);
}

static void
mark_used(struct birdcage_hdr* hdr)
{
  hdr->next = (void*)hdr->next + 1;
}

static void
mark_free(struct birdcage_hdr* hdr)
{
  hdr->next = (void*)hdr->next - 1;
}

struct birdcage
birdcage_create(void* buffer, size_t buffer_size)
{
  struct birdcage out;
  out.ptr = buffer;
  out.capacity = buffer_size;

  for (unsigned i = 0; i < BIRDCAGE_BUCKET_END - BIRDCAGE_BUCKET_START; i++)
    out.buckets[i].first_free = 0;

  *(struct birdcage_hdr*)out.ptr = (struct birdcage_hdr){
    .next = 0,
    .prev = 0,
    .size = buffer_size - sizeof(struct birdcage_hdr),
  };

  insert(&out, out.ptr);

  return out;
}

/* intermediate data structure that holds information for
 * a pending suitable allocation */
struct suitable
{
  /* amount of padding immediately after the allocation header
   * and before the end user reachable allocation such that
   * the requested alignment for said allocation is provided */
  size_t prepadding;

  /* prepadding + allocation_size + postpadding */
  /* postpadding is computed inline and not stored seperately */
  size_t total_consumption;

  /* node before the `this` allocation
   * if 0, then `this` is the start of the list */
  struct birdcage_hdr* this;
};

struct birdcage_hdr*
immediate_next(struct birdcage* cage, struct birdcage_hdr* restrict hdr)
{
  struct birdcage_hdr* out = (void*)(hdr + 1) + hdr->size;
  if ((uintptr_t)out >= (uintptr_t)cage->ptr + cage->capacity)
    return 0;
  return out;
}

static size_t
aligned_addr(uintptr_t const addr, uintptr_t const alignment)
{
  return ((addr + (alignment - 1)) & ~(alignment - 1));
}

static size_t
alignment_padding(uintptr_t const addr, uintptr_t const alignment)
{
  return aligned_addr(addr, alignment) - addr;
}

static size_t
prepadding_alignment(struct birdcage_hdr* hdr, size_t alignment)
{
  return alignment_padding((uintptr_t)(hdr + 1), alignment);
}

static void*
alloc_start(struct suitable const s)
{
  void* p = s.this + 1;
  return p + s.prepadding;
}

static size_t
total_allocation_size(struct birdcage_hdr* header_address,
                      size_t size,
                      size_t alignment)
{
  void* const living_address = header_address + 1;
  uintptr_t const living_addressp = (uintptr_t)living_address;
  uintptr_t const pre_padding = prepadding_alignment(header_address, alignment);
  uintptr_t const post_padding =
    alignment_padding(living_addressp + pre_padding + size, HDR_ALIGNMENT);
  return pre_padding + size + post_padding;
}

static struct suitable
find_suitable_allocation_from_bucket(struct birdcage_bucket* bucket,
                                     size_t size,
                                     size_t alignment)
{
  struct birdcage_hdr* hdr = bucket->first_free;

  for (; hdr; hdr = hdr->next) {
    uintptr_t const total_consumption =
      total_allocation_size(hdr, size, alignment);

    if (total_consumption <= hdr->size) {
      struct suitable out;
      out.prepadding = prepadding_alignment(hdr, alignment);
      out.total_consumption = total_consumption;
      out.this = hdr;
      return out;
    }
  }

  struct suitable out;
  out.this = 0;
  return out;
}

static struct suitable
find_suitable_allocation(struct birdcage* restrict cage,
                         size_t size,
                         size_t alignment)
{
  unsigned bi = bucket_idx_for_size(size);

  for (; bi < BIRDCAGE_BUCKET_END - BIRDCAGE_BUCKET_START; bi++) {
    struct suitable const s =
      find_suitable_allocation_from_bucket(&cage->buckets[bi], size, alignment);
    if (s.this)
      return s;
  }

  struct suitable out;
  out.this = 0;
  return out;
}

static void
split(struct birdcage* restrict cage,
      struct birdcage_hdr* this,
      uintptr_t const consumption)
{

  size_t const size = this->size;

  /* remember to include space for the header */
  size_t const size_after_split = size - consumption;
  if (size_after_split < MIN_BLOCK_SIZE + sizeof(struct birdcage_hdr))
    return;

  this->size = consumption;

  struct birdcage_hdr* target = (void*)(this + 1) + consumption;
  target->size = size_after_split - sizeof(struct birdcage_hdr);
  target->prev = 0;
  target->next = 0;
  insert(cage, target);
}

static void
coalesce(struct birdcage* restrict cage, struct birdcage_hdr* hdr)
{
  struct birdcage_hdr* imm_next = immediate_next(cage, hdr);

  if (imm_next && is_free(imm_next)) {
    struct birdcage_bucket* original_bucket = hdrs_bucket(cage, hdr);

    remove_from_list(hdrs_bucket(cage, imm_next), imm_next);
    hdr->size += imm_next->size + sizeof *imm_next;
    struct birdcage_bucket* new_bucket = hdrs_bucket(cage, hdr);

    if (original_bucket != new_bucket) {
      remove_from_list(original_bucket, hdr);
      add_to_list(new_bucket, hdr);
    }
  }
}

static void
coalesce_bucket(struct birdcage* restrict cage,
                struct birdcage_bucket* restrict bucket)
{
  struct birdcage_hdr* hdr = bucket->first_free;

  while (hdr) {
    struct birdcage_hdr* const next = hdr->next;
    coalesce(cage, hdr);
    hdr = next;
  }
}

static void
coalesce_everything(struct birdcage* restrict cage)
{
  for (unsigned i = 0; i < BIRDCAGE_BUCKET_END - BIRDCAGE_BUCKET_START; i++)
    coalesce_bucket(cage, &cage->buckets[i]);
}

static void
make_used(struct suitable suitable)
{
  mark_used(suitable.this);
  uintptr_t* dist_ptr = ((uintptr_t*)alloc_start(suitable)) - 1;
  *dist_ptr = (uintptr_t)dist_ptr - (uintptr_t)suitable.this;
}

static struct suitable
try_find_coalesce(struct birdcage* restrict cage,
                  size_t const size,
                  size_t const alignment)
{
  enum
  {
    /* since the free-list is not traversable in-order (LSA->MSA),
     * it may take multiple coalescing attempts to properly
     * group together free allocations and get a buffer
     * that is large enough for the requested allocation.
     * therefore, try multiple times, then fail. 3 sounds good. */
    MAX_TRIES = 3,
  };

  for (unsigned i = 0; i < MAX_TRIES; i++) {
    struct suitable const s = find_suitable_allocation(cage, size, alignment);
    if (s.this)
      return s;
    coalesce_everything(cage);
  }

  return (struct suitable){ .this = 0 };
}

void*
birdcage_alloc_ex(struct birdcage* restrict cage, size_t size, size_t alignment)
{
  if (size == 0)
    return 0;

  alignment = MAX(alignment, HDR_ALIGNMENT);

  struct suitable const s = try_find_coalesce(cage, size, alignment);
  if (!s.this)
    return 0;

  /* split if needed */
  remove_from_list(hdrs_bucket(cage, s.this), s.this);
  split(cage, s.this, s.total_consumption);
  make_used(s);

  return alloc_start(s);
}

static struct birdcage_hdr*
extract_header(void* const ptr)
{
  uintptr_t* const ptr2 = (uintptr_t*)ptr - 1;
  struct birdcage_hdr* out = (void*)ptr2 - *ptr2;
  return out;
}

void
birdcage_free(struct birdcage* restrict cage, void* const ptr)
{
  if (!ptr)
    return;

  struct birdcage_hdr* hdr = extract_header(ptr);
  mark_free(hdr);
  add_to_list(hdrs_bucket(cage, hdr), hdr);
  coalesce(cage, hdr);
}

static void*
full_realloc(struct birdcage* restrict cage,
             void* const ptr,
             size_t const newsize,
             size_t const newalignment,
             size_t const old_size)
{
  void* out = birdcage_alloc_ex(cage, newsize, newalignment);
  memcpy(out, ptr, MIN(old_size, newsize));
  birdcage_free(cage, ptr);
  return out;
}

static void*
try_shrink(struct birdcage* restrict cage,
           struct birdcage_hdr* const this_hdr,
           void* const ptr,
           size_t const consumption)
{
  split(cage, this_hdr, consumption);
  struct birdcage_hdr* imm_next = immediate_next(cage, this_hdr);
  assert(imm_next);
  /* try to coalesce the freely created new block */
  coalesce(cage, imm_next);
  return ptr;
}

/* attempts to "eat" part of the next allocation, if free.
 * "how much" is the byte amount of allocation to attempted
 * to be added to the header */
static void
eat_next(struct birdcage* restrict cage,
         struct birdcage_hdr* hdr,
         size_t const how_much)
{
  struct birdcage_hdr* imm_next = immediate_next(cage, hdr);
  if (is_used(imm_next))
    return;
  if (how_much > imm_next->size || imm_next->size - how_much < MIN_BLOCK_SIZE)
    return;

  remove_from_list(hdrs_bucket(cage, imm_next), imm_next);
  struct birdcage_hdr* new = (void*)imm_next + how_much;
  new->size = imm_next->size - how_much;
  new->next = 0;
  new->prev = 0;
  hdr->size += how_much;
  insert(cage, new);
}

void*
birdcage_realloc_ex(struct birdcage* restrict cage,
                    void* ptr,
                    size_t const newsize,
                    size_t const newalignment)
{
  if (ptr == 0)
    return birdcage_alloc_ex(cage, newsize, newalignment);

  struct birdcage_hdr* this_hdr = extract_header(ptr);

  if (alignment_padding((uintptr_t)ptr, newalignment) != 0)
    return full_realloc(cage, ptr, newsize, newalignment, this_hdr->size);

  size_t const consumption =
    total_allocation_size(this_hdr, newsize, newalignment);

  if (this_hdr->size == consumption)
    return ptr;

  if (consumption > this_hdr->size) {
    eat_next(cage, this_hdr, newsize - this_hdr->size);
    if (this_hdr->size < newsize)
      return full_realloc(cage, ptr, newsize, newalignment, this_hdr->size);
    return ptr;
  }

  return try_shrink(cage, this_hdr, ptr, consumption);
}

/* provide an extern definition incase the compiler decides
 * to do so. clangd will complain about this bein redundant,
 * it is actually lying (at least in the case of GCC compiling) */
extern inline void*
birdcage_alloc(struct birdcage* cage, size_t size);
extern inline void*
birdcage_realloc(struct birdcage* restrict cage, void* ptr, size_t size);
