#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

#include <twiddle/bitmap/bitmap.h>

static inline void tw_bitmap_clear_extra_bits(struct tw_bitmap *bitmap)
{
  const uint64_t size = bitmap->info.size;
  const uint64_t rest = size % TW_BITS_PER_BITMAP;
  if (rest) {
    bitmap->data[BITMAP_POS(size)] ^= ~0UL << rest;
  }
}

struct tw_bitmap *tw_bitmap_new(uint64_t size)
{
  if (0 == size || size > TW_BITMAP_MAX_BITS) {
    return NULL;
  }

  struct tw_bitmap *bitmap = calloc(1, sizeof(struct tw_bitmap));

  if (!bitmap) {
    return NULL;
  }

  const size_t data_size =
      TW_ALLOC_TO_CACHELINE(TW_BITMAP_PER_BITS(size) * TW_BYTES_PER_BITMAP);

  if (posix_memalign((void *)&(bitmap->data), TW_CACHELINE, data_size)) {
    free(bitmap);
    return NULL;
  }

  memset(bitmap->data, 0, data_size);

  bitmap->info = tw_bitmap_info_init(data_size * TW_BITS_IN_WORD);
  return bitmap;
}

void tw_bitmap_free(struct tw_bitmap *bitmap) { free(bitmap); }

struct tw_bitmap *tw_bitmap_copy(const struct tw_bitmap *src,
                                 struct tw_bitmap *dst)
{
  assert(src && dst);

  /**
   * When bitmaps size are not equal, memory fragmentation becomes a problem
   *if
   * one consecutively copy from a decreasing size bitmap, e.g.:
   *
   *   a.size == 32, b.size == 32, c.size == 31;
   *
   *   assert(tw_bitmap_copy(a, b) == b);  // ok
   *   assert(tw_bitmap_copy(c, b) == b);  // ok
   *   assert(tw_bitmap_copy(a, b) == b);  // fails because b.size is now 31
   *
   * No memory leaks are involved since calls to tw_bitmap_free don't depends
   * on bitmap->info.size;
   */
  if (tw_unlikely(dst->info.size != src->info.size)) {
    return NULL;
  }

  tw_bitmap_info_copy(src->info, dst->info);
  memcpy(dst->data, src->data,
         TW_BITMAP_PER_BITS(src->info.size) * TW_BYTES_PER_BITMAP);

  return dst;
}

struct tw_bitmap *tw_bitmap_clone(const struct tw_bitmap *bitmap)
{
  assert(bitmap);

  struct tw_bitmap *new = tw_bitmap_new(bitmap->info.size);
  if (!new) {
    return NULL;
  }

  return tw_bitmap_copy(bitmap, new);
}

void inline tw_bitmap_set(struct tw_bitmap *bitmap, uint64_t pos)
{
  assert(bitmap && pos < bitmap->info.size);

  const bitmap_t old_bitmap = bitmap->data[BITMAP_POS(pos)];
  const bitmap_t new_bitmap = old_bitmap | MASK(pos);
  const bool changed = (old_bitmap != new_bitmap);
  bitmap->info.count += changed;
  bitmap->data[BITMAP_POS(pos)] = new_bitmap;
}

void inline tw_bitmap_clear(struct tw_bitmap *bitmap, uint64_t pos)
{
  assert(bitmap && pos < bitmap->info.size);

  const bitmap_t old_bitmap = bitmap->data[BITMAP_POS(pos)];
  const bitmap_t new_bitmap = old_bitmap & ~MASK(pos);
  const bool changed = (old_bitmap != new_bitmap);
  bitmap->info.count -= changed;
  bitmap->data[BITMAP_POS(pos)] = new_bitmap;
}

bool tw_bitmap_test(const struct tw_bitmap *bitmap, uint64_t pos)
{
  assert(bitmap && pos < bitmap->info.size);
  return !!(bitmap->data[BITMAP_POS(pos)] & MASK(pos));
}

bool tw_bitmap_test_and_set(struct tw_bitmap *bitmap, uint64_t pos)
{
  assert(bitmap && pos < bitmap->info.size);

  const bitmap_t old_bitmap = bitmap->data[BITMAP_POS(pos)];
  const bitmap_t new_bitmap = old_bitmap | MASK(pos);
  const bool changed = (old_bitmap != new_bitmap);
  bitmap->info.count += changed;
  bitmap->data[BITMAP_POS(pos)] = new_bitmap;
  return !changed;
}

bool tw_bitmap_test_and_clear(struct tw_bitmap *bitmap, uint64_t pos)
{
  assert(bitmap && pos < bitmap->info.size);

  const bitmap_t old_bitmap = bitmap->data[BITMAP_POS(pos)];
  const bitmap_t new_bitmap = old_bitmap & ~MASK(pos);
  const bool changed = (old_bitmap != new_bitmap);
  bitmap->info.count -= changed;
  bitmap->data[BITMAP_POS(pos)] = new_bitmap;
  return changed;
}

bool tw_bitmap_empty(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_empty(bitmap->info);
}

bool tw_bitmap_full(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_full(bitmap->info);
}

uint64_t tw_bitmap_count(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_count(bitmap->info);
}

float tw_bitmap_density(const struct tw_bitmap *bitmap)
{
  assert(bitmap);
  return tw_bitmap_info_density(bitmap->info);
}

struct tw_bitmap *tw_bitmap_zero(struct tw_bitmap *bitmap)
{
  assert(bitmap);

  memset(bitmap->data, 0,
         TW_BITMAP_PER_BITS(bitmap->info.size) * TW_BYTES_PER_BITMAP);

  bitmap->info.count = 0U;

  return bitmap;
}

struct tw_bitmap *tw_bitmap_fill(struct tw_bitmap *bitmap)
{
  assert(bitmap);

  memset(bitmap->data, 0xFF,
         TW_BITMAP_PER_BITS(bitmap->info.size) * TW_BYTES_PER_BITMAP);

  tw_bitmap_clear_extra_bits(bitmap);

  bitmap->info.count = bitmap->info.size;

  return bitmap;
}

int64_t tw_bitmap_find_first_zero(const struct tw_bitmap *bitmap)
{
  assert(bitmap);

  /**
   * This check is required since we allocate memory on multiple of bitmap_t.
   * Thus if bitmap.size is not aligned on 64 bits, a full bitmap
   * would incorrectly report nbits+1 as the position of the first zero.
   */
  if (tw_unlikely(tw_bitmap_full(bitmap))) {
    return -1;
  }

  for (size_t i = 0; i < TW_BITMAP_PER_BITS(bitmap->info.size); ++i) {
    const int pos = tw_ffzll(bitmap->data[i]);
    if (pos) {
      return (i * TW_BITS_PER_BITMAP) + (pos - 1);
    }
  }

  return -1;
}

int64_t tw_bitmap_find_first_bit(const struct tw_bitmap *bitmap)
{
  assert(bitmap);

  /**
   * This check is required since we allocate memory on multiple of bitmap_t
   * Thus if bitmap.size is not aligned on 64 bits, a full bitmap
   * filled with tw_bitmap_fill and then cleared manually with
   * tw_bitmap_clear,
   * could report the first bit to be greater than nbits.
   */
  if (tw_unlikely(tw_bitmap_empty(bitmap))) {
    return -1;
  }

  for (size_t i = 0; i < TW_BITMAP_PER_BITS(bitmap->info.size); ++i) {
    const int pos = tw_ffsll(bitmap->data[i]);
    if (pos) {
      return (i * TW_BITS_PER_BITMAP) + (pos - 1);
    }
  }

  return -1;
}

#define BITMAP_NOT_LOOP(simd_t, simd_set1, simd_load, simd_xor, simd_store)    \
  const simd_t mask = simd_set1(~0);                                           \
  for (size_t i = 0; i < TW_VECTOR_PER_BITS(bitmap->info.size); ++i) {         \
    simd_t *addr = (simd_t *)bitmap->data + i;                                 \
    const simd_t src = simd_load(addr);                                        \
    const simd_t res = simd_xor(src, mask);                                    \
    simd_store(addr, res);                                                     \
  }

struct tw_bitmap *tw_bitmap_not(struct tw_bitmap *bitmap)
{
  assert(bitmap);

#ifdef USE_AVX512
  BITMAP_NOT_LOOP(__m512i, _mm512_set1_epi8, _mm512_load_si512,
                  _mm512_xor_si512, _mm512_store_si512)
#elif USE_AVX2
  BITMAP_NOT_LOOP(__m256i, _mm256_set1_epi8, _mm256_load_si256,
                  _mm256_xor_si256, _mm256_store_si256)
#elif USE_AVX
  BITMAP_NOT_LOOP(__m128i, _mm_set1_epi8, _mm_load_si128, _mm_xor_si128,
                  _mm_store_si128)
#else
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(bitmap->info.size); ++i) {
    bitmap->data[i] ^= ~0UL;
  }
#endif

  bitmap->info.count = bitmap->info.size - bitmap->info.count;

  return bitmap;
}

#define BITMAP_EQ_LOOP(simd_t, simd_load, simd_cmpeq, simd_maskmove, eq_mask)  \
  for (size_t i = 0; i < size / (sizeof(simd_t) * TW_BITS_IN_WORD); ++i) {     \
    simd_t *a_addr = (simd_t *)a->data + i, *b_addr = (simd_t *)b->data + i;   \
    const simd_t v_cmp = simd_cmpeq(simd_load(a_addr), simd_load(b_addr));     \
    const int h_cmp = simd_maskmove(v_cmp);                                    \
    if (h_cmp != eq_mask) {                                                    \
      return false;                                                            \
    }                                                                          \
  }

bool tw_bitmap_equal(const struct tw_bitmap *a, const struct tw_bitmap *b)
{
  assert(a && b);

  if (a->info.size != b->info.size || a->info.count != b->info.count) {
    return false;
  }

  const uint64_t size = a->info.size;

/* AVX512 does not have movemask_epi8 equivalent, fallback to AVX2 */
#if USE_AVX2
  BITMAP_EQ_LOOP(__m256i, _mm256_load_si256, _mm256_cmpeq_epi8,
                 _mm256_movemask_epi8, 0xFFFFFFFF)
#elif USE_AVX
  BITMAP_EQ_LOOP(__m128i, _mm_load_si128, _mm_cmpeq_epi8, _mm_movemask_epi8,
                 0xFFFF)
#else
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(size); ++i) {
    if (a->data[i] != b->data[i]) {
      return false;
    }
  }
#endif

  return true;
}

#define BITMAP_OP_LOOP(simd_t, simd_load, simd_op, simd_store)                 \
  const size_t uint64_t_per_simd_t = sizeof(simd_t) / sizeof(uint64_t);        \
  for (size_t i = 0; i < size / (sizeof(simd_t) * TW_BITS_IN_WORD); ++i) {     \
    simd_t *src_vec = (simd_t *)src->data + i,                                 \
           *dst_vec = (simd_t *)dst->data + i;                                 \
    const simd_t res = simd_op(simd_load(src_vec), simd_load(dst_vec));        \
    simd_store(dst_vec, res);                                                  \
    for (size_t j = 0; j < uint64_t_per_simd_t; j++) {                         \
      count += tw_popcountl(dst->data[i * uint64_t_per_simd_t + j]);           \
    }                                                                          \
  }

struct tw_bitmap *tw_bitmap_union(const struct tw_bitmap *src,
                                  struct tw_bitmap *dst)
{
  assert(src && dst);

  if (src->info.size != dst->info.size) {
    return NULL;
  }

  const uint64_t size = src->info.size;

  uint64_t count = 0;
#if USE_AVX512
  BITMAP_OP_LOOP(__m512i, _mm512_load_si512, _mm512_or_si512,
                 _mm512_store_si512)
#elif USE_AVX2
  BITMAP_OP_LOOP(__m256i, _mm256_load_si256, _mm256_or_si256,
                 _mm256_store_si256)
#elif USE_AVX
  BITMAP_OP_LOOP(__m128i, _mm_load_si128, _mm_or_si128, _mm_store_si128)
#else
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(size); ++i) {
    dst->data[i] |= src->data[i];
    count += tw_popcountl(dst->data[i]);
  }
#endif

  dst->info.count = count;

  return dst;
}

struct tw_bitmap *tw_bitmap_intersection(const struct tw_bitmap *src,
                                         struct tw_bitmap *dst)
{
  assert(src && dst);

  if (src->info.size != dst->info.size) {
    return NULL;
  }

  const uint64_t size = src->info.size;

  uint64_t count = 0;
#if USE_AVX512
  BITMAP_OP_LOOP(__m512i, _mm512_load_si512, _mm512_and_si512,
                 _mm512_store_si512)
#elif USE_AVX2
  BITMAP_OP_LOOP(__m256i, _mm256_load_si256, _mm256_and_si256,
                 _mm256_store_si256)
#elif USE_AVX
  BITMAP_OP_LOOP(__m128i, _mm_load_si128, _mm_and_si128, _mm_store_si128)
#else
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(size); ++i) {
    dst->data[i] &= src->data[i];
    count += tw_popcountl(dst->data[i]);
  }
#endif

  dst->info.count = count;

  return dst;
}

struct tw_bitmap *tw_bitmap_xor(const struct tw_bitmap *src,
                                struct tw_bitmap *dst)
{
  assert(src && dst);

  if (src->info.size != dst->info.size) {
    return NULL;
  }

  const uint64_t size = src->info.size;

  uint64_t count = 0;
#if USE_AVX512
  BITMAP_OP_LOOP(__m512i, _mm512_load_si512, _mm512_xor_si512,
                 _mm512_store_si512)
#elif USE_AVX2
  BITMAP_OP_LOOP(__m256i, _mm256_load_si256, _mm256_xor_si256,
                 _mm256_store_si256)
#elif USE_AVX
  BITMAP_OP_LOOP(__m128i, _mm_load_si128, _mm_xor_si128, _mm_store_si128)
#else
  for (size_t i = 0; i < TW_BITMAP_PER_BITS(size); ++i) {
    dst->data[i] ^= src->data[i];
    count += tw_popcountl(dst->data[i]);
  }
#endif

  dst->info.count = count;

  return dst;
}
