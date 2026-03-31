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

#include "birdcage.h"

#define MAX(x, y) ((x > y) ? x : y)

enum
{
  /* also works as the minimum alignment */
  HDR_ALIGNMENT = 4,
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
  if (bucket->first_free)
    bucket->first_free->prev = hdr;
  hdr->next = bucket->first_free;
  bucket->first_free = hdr;
}

static void
remove_from_list(struct birdcage_bucket* bucket,
                 struct birdcage_hdr* hind,
                 struct birdcage_hdr* hdr)
{
  if (hind)
    hind->next = hdr->next;
  else
    bucket->first_free = hdr->next;
  if (hdr->next)
    hdr->next->prev = hind;
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

static void
mark_used(struct birdcage_hdr* hdr)
{
  hdr->next = (void*)hdr->next + 1;
}

static void
mark_free(struct birdcage_hdr* restrict hdr)
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
  struct birdcage_hdr* hind;
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
alignment_padding(uintptr_t addr, size_t alignment)
{
  return (alignment - (addr % alignment)) % alignment;
}

static size_t
prepadding_alignment(struct birdcage_hdr* hdr, size_t alignment)
{
  size_t out = alignment_padding((uintptr_t)(hdr + 1), alignment);
  return out;
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
  struct birdcage_hdr* hind = 0;

  for (; hdr; hind = hdr, hdr = hdr->next) {
    uintptr_t const total_consumption =
      total_allocation_size(hdr, size, alignment);

    if (total_consumption <= hdr->size) {
      struct suitable out;
      out.prepadding = prepadding_alignment(hdr, alignment);
      out.total_consumption = total_consumption;
      out.this = hdr;
      out.hind = hind;
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
split(struct birdcage* restrict cage, struct suitable suitable)
{
  enum
  {
    MIN_BLOCK_SIZE =
      (1U << BIRDCAGE_BUCKET_START) + sizeof(struct birdcage_hdr),
  };

  size_t const size = suitable.this->size;

  /* remember to include space for the header */
  size_t const size_after_split =
    size - suitable.total_consumption - sizeof(struct birdcage_hdr);

  if (size_after_split < MIN_BLOCK_SIZE)
    return;

  suitable.this->size = suitable.total_consumption;

  struct birdcage_hdr* target =
    (void*)(suitable.this + 1) + suitable.total_consumption;
  target->size = size_after_split;
  target->prev = 0;
  insert(cage, target);
}

/* will insert the provided header back into a bucket */
static void
coalesce(struct birdcage* restrict cage, struct birdcage_hdr* hdr)
{
  struct birdcage_hdr* imm_next = immediate_next(cage, hdr);

  if (imm_next && !is_used(imm_next)) {
    hdr->size += imm_next->size + sizeof *imm_next;
    if (imm_next->prev)
      imm_next->prev->next = imm_next->next;
    else
      hdrs_bucket(cage, hdr)->first_free = imm_next->next;
  }

  insert(cage, hdr);
}

static void
coalesce_bucket(struct birdcage* restrict cage,
                struct birdcage_bucket* restrict bucket)
{
  struct birdcage_hdr* hdr = bucket->first_free;
  struct birdcage_hdr* hind = 0;
  while (hdr) {
    remove_from_list(bucket, hind, hdr);
    coalesce(cage, hdr);
    hind = hdr;
    hdr = hdr->next;
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

void*
birdcage_alloc_ex(struct birdcage* restrict cage, size_t size, size_t alignment)
{
  alignment = MAX(alignment, HDR_ALIGNMENT);

  bool try_once = false;
  struct suitable s;
try_again:
  s = find_suitable_allocation(cage, size, alignment);

  if (!s.this) {
    if (!try_once) {
      try_once = true;
      /* coalesce the entire heap (expensive!)
       * and then try again
       * another alternative would be only to coalesce linearly
       * until we can create a large enough block to fit our alloc,
       * then quit early */
      coalesce_everything(cage);
      goto try_again;
    }

    return 0;
  }

  /* split if needed */
  remove_from_list(hdrs_bucket(cage, s.this), s.hind, s.this);
  split(cage, s);
  make_used(s);

  return alloc_start(s);
}

static struct birdcage_hdr*
extract_header(void* ptr)
{
  uintptr_t* ptr2 = (uintptr_t*)ptr - 1;
  struct birdcage_hdr* out = (void*)ptr2 - *ptr2;
  mark_free(out);
  return out;
}

void
birdcage_free(struct birdcage* restrict cage, void* ptr)
{
  struct birdcage_hdr* hdr = extract_header(ptr);
  coalesce(cage, hdr);
}

/* provide an extern definition incase the compiler decides
 * to do so. clangd will complain about this bein redundant,
 * it is actually lying (at least in the case of GCC compiling) */
extern inline void*
birdcage_alloc(struct birdcage* cage, size_t size);
