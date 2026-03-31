#include "include/birdcage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t
get_bucket_size(struct birdcage_bucket const* bucket)
{
  size_t size = 0;
  struct birdcage_hdr* hdr = bucket->first_free;
  while (hdr)
    hdr = hdr->next, size++;
  return size;
}

static void
debug_print(struct birdcage* cage)
{
  for (unsigned i = 0; i < BIRDCAGE_BUCKET_END - BIRDCAGE_BUCKET_START; i++)
    printf("B%0.2i {%i}, ", i, get_bucket_size(&cage->buckets[i]));
  putchar('\n');
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

  int* i = birdcage_alloc(&cage, sizeof(int));
  if (!i)
    printf("NULL!\n"), exit(1);
  *i = 4;
  printf("%i\n", *i);

  int* i2 = birdcage_alloc(&cage, sizeof(int));
  *i2 = 3;
  printf("%i\n", *i2);

  char const str[] = "hello, world!";
  char* str2 = birdcage_alloc(&cage, sizeof str);
  strcpy(str2, str);
  printf("%s\n", str2);

  alignof(str);

  birdcage_free(&cage, i2);
  birdcage_free(&cage, i);
  birdcage_free(&cage, str2);

  char* buf = birdcage_alloc(&cage, 1024);
  birdcage_free(&cage, buf);

  debug_print(&cage);

  free(internal_buffer);
}
