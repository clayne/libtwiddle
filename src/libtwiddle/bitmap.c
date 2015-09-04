#include <assert.h>
#include <stdlib.h>
#include <libtwiddle/bitmap.h>

struct tw_bitmap *
tw_bitmap_new(uint32_t nbits)
{
  assert(nbits <= UINT32_MAX);
  struct tw_bitmap *bitmap = calloc(1,
                                    sizeof(struct tw_bitmap_info) +
                                      TW_BITMAP_PER_BITS(nbits) * TW_BYTES_PER_BITMAP);
  bitmap->info = tw_bitmap_info_init(nbits);
  return bitmap;
}

void
tw_bitmap_free(struct tw_bitmap *bitmap)
{
  free(bitmap);
}

void
tw_bitmap_set(struct tw_bitmap *bitmap, uint32_t pos)
{
  tw_bitmap_test_and_set(bitmap, pos);
}

void
tw_bitmap_clear(struct tw_bitmap *bitmap, uint32_t pos)
{
  tw_bitmap_test_and_clear(bitmap, pos);
}

bool
tw_bitmap_test(const struct tw_bitmap *bitmap, uint32_t pos)
{
  assert(bitmap && pos <= bitmap->info.size);
  return !!(bitmap->data[TW_BYTE_POS(pos)] & TW_BIT_POS(pos));
}

bool
tw_bitmap_test_and_set(struct tw_bitmap *bitmap, uint32_t pos)
{
  assert(bitmap && pos <= bitmap->info.size);
  char *addr = (char *) &(bitmap->data[TW_BYTE_POS(pos)]);
  const bool prev = (*addr) & TW_BIT_POS(pos);

  if(!prev)
    bitmap->info.count++;

  *addr |= TW_BIT_POS(pos);

  return prev;
}

bool
tw_bitmap_test_and_clear(struct tw_bitmap *bitmap, uint32_t pos)
{
  assert(bitmap && pos <= bitmap->info.size);
  char *addr = (char *) &(bitmap->data[TW_BYTE_POS(pos)]);
  const bool prev = (*addr) & TW_BIT_POS(pos);

  if(prev)
    bitmap->info.count--;

  *addr &= ~TW_BIT_POS(pos);

  return prev;
}

bool
tw_bitmap_empty(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_empty(bitmap->info);
}

bool
tw_bitmap_full(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_full(bitmap->info);
}

uint32_t
tw_bitmap_count(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_count(bitmap->info);
}

void
tw_bitmap_zero(struct tw_bitmap *bitmap)
{
  assert(bitmap);
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(bitmap->info.size); ++i)
    bitmap->data[i] = 0UL;

  bitmap->info.count = 0U;
}

void
tw_bitmap_fill(struct tw_bitmap *bitmap)
{
  assert(bitmap);
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(bitmap->info.size); ++i)
    bitmap->data[i] = ~0UL;

  bitmap->info.count = bitmap->info.size;
}

