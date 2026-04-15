#include "include/birdcage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct size
{
  size_t s, m;
};

static struct size
get_bucket_size(struct birdcage_bucket const* bucket)
{
  size_t size = 0;
  size_t total_allocs = 0;
  struct birdcage_hdr* hdr = bucket->first_free;

  while (hdr) {
    total_allocs += hdr->size + sizeof(*hdr);
    hdr = hdr->next;
    size++;
  }

  return (struct size){ .s = size, .m = total_allocs };
}

static void
debug_print(struct birdcage* cage)
{
  size_t q = 0;
  for (unsigned i = 0; i < BIRDCAGE_BUCKET_END - BIRDCAGE_BUCKET_START; i++) {
    struct size m = get_bucket_size(&cage->buckets[i]);
    printf("B%0.2i {%li %#lx}, ", i, m.s, m.m);
    q += m.m;
  }
  printf("\n  total: %#lx\n", q);
}

void
simple_test(struct birdcage* cage)
{
  enum
  {
    num_alloc = 128,
  };

  void* al[num_alloc];
  unsigned m = 0;
  for (unsigned i = 1; i < num_alloc; i++) {
    int* v = birdcage_alloc(cage, sizeof(int) * i);
    m += i;
    if (v == 0)
      printf("NULL!\n");
    *v = 24;
    al[i] = v;
  }
  printf("size: %i\n", m);
  for (unsigned i = 0; i < num_alloc; i++)
    birdcage_free(cage, al[i]);
}

void
manual_realloc_test(struct birdcage* cage)
{
  enum
  {
    times = 64
  };

  int* ptr = birdcage_alloc(cage, sizeof(int));

  for (unsigned i = 0; i < times; i++) {
    int* newptr = birdcage_alloc(cage, sizeof(int));
    memcpy(newptr, ptr, sizeof *ptr);
    birdcage_free(cage, ptr);
    ptr = newptr;
  }

  birdcage_free(cage, ptr);
}

void
realloc_test(struct birdcage* cage)
{
  enum
  {
    times = 64
  };

  int* ptr = birdcage_alloc(cage, sizeof(int));
  *ptr = 25023;
  int* init = ptr;

  for (unsigned i = 0; i < times; i++) {
    ptr = birdcage_realloc(cage, ptr, sizeof(int) + i * 4);
  }

  printf("%p %p, %i\n", init, ptr, *ptr);
  birdcage_free(cage, ptr);
}

void
shrink_test(struct birdcage* cage)
{
  enum
  {
    times = 64,
  };

  int* ptr = birdcage_alloc(cage, sizeof(int));
  *ptr = 25023;
  int i = times;
  while (i-- > 1) {
    ptr = birdcage_realloc(cage, ptr, sizeof(int) * i);
  }

  printf("%i\n", *ptr);

  birdcage_free(cage, ptr);
}

int
main()
{
  enum
  {
    size = 1024 * 64,
  };

  void* internal_buffer = malloc(size);
  struct birdcage cage = birdcage_create(internal_buffer, size);

  // manual_realloc_test(&cage);
  // realloc_test(&cage);
  shrink_test(&cage);

  // debug_print(&cage);

  void* ptr = birdcage_alloc(&cage, 1024);
  printf("%i\n", cage.usage);
  birdcage_free(&cage, ptr);
  printf("%i\n", cage.usage);

  // char* x = birdcage_alloc(&cage, 1);
  // for (unsigned i = 1; i < 64; i++)
  //   x = birdcage_realloc(&cage, x, i);

  // debug_print(&cage);
  // void* s = birdcage_alloc(&cage, 60000);
  // printf("%p\n", s);
  // debug_print(&cage);

  free(internal_buffer);
}
