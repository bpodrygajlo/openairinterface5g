/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "fh_compression.h"
#include "assertions.h"
#include <limits.h>
#include <string.h>
#include <simde/x86/avx2.h>

/* ---- bit-stream helpers ---- */

// A PRB is FH_VALS_PER_PRB values of `width` bits, MSB first, back to back: 3 * width bytes, so the
// stream always ends on a byte boundary. Stream it through a 64-bit accumulator a whole byte at a
// time instead of addressing each value's bits individually. Both are forced inline so that a caller
// passing a constant width gets a fully unrolled, branch-free packer for that width.
#define FH_INLINE static inline __attribute__((always_inline))

FH_INLINE void pack_prb(uint8_t *out, const int32_t *vals, int width)
{
  const uint32_t mask = (1u << width) - 1;
  uint64_t acc = 0;
  int nbits = 0;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    acc = (acc << width) | ((uint32_t)vals[i] & mask);
    nbits += width;
    while (nbits >= 8) {
      nbits -= 8;
      *out++ = (uint8_t)(acc >> nbits);
    }
  }
}

FH_INLINE void unpack_prb(const uint8_t *in, int32_t *vals, int width)
{
  uint64_t acc = 0;
  int nbits = 0;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    while (nbits < width) {
      acc = (acc << 8) | *in++;
      nbits += 8;
    }
    nbits -= width;
    // Move the value's sign bit to bit 63, then arithmetic-shift it back down.
    vals[i] = (int32_t)((int64_t)(acc << (64 - width - nbits)) >> (64 - width));
  }
}

/* ---- BFP ---- */

static uint16_t saturating_abs16(int16_t v)
{
  if (v == INT16_MIN)
    return INT16_MAX;
  return (uint16_t)(v < 0 ? -v : v);
}

static int leading_zero_count16(uint16_t v)
{
  return v == 0 ? 16 : __builtin_clz(v) - 16;
}

FH_INLINE int bfp_exponent(uint16_t max_abs, int iq_bits)
{
  int exponent = 16 - iq_bits + 1 - leading_zero_count16(max_abs < 1 ? 1 : max_abs);
  return exponent < 0 ? 0 : exponent;
}

// Eight consecutive values of w bits are exactly w bytes. These keep such a group MSB-first in the
// low 8 * w bits of a 128-bit integer and move it to/from the wire big-endian, touching only its
// w bytes.
FH_INLINE void put_group_be(uint8_t *out, unsigned __int128 group, int w)
{
  group <<= 128 - 8 * w;
  const uint64_t be[2] = {__builtin_bswap64((uint64_t)(group >> 64)), __builtin_bswap64((uint64_t)group)};
  memcpy(out, be, w);
}

FH_INLINE unsigned __int128 get_group_be(const uint8_t *in, int w)
{
  uint64_t be[2] = {0, 0};
  memcpy(be, in, w);
  const unsigned __int128 group = ((unsigned __int128)__builtin_bswap64(be[0]) << 64) | __builtin_bswap64(be[1]);
  return group >> (128 - 8 * w);
}

// Scalar BFP for any width: the reference for the vector path below.
FH_INLINE void bfp_compress_prb_scalar(const int16_t *src, int8_t *dst, int iq_bits)
{
  uint16_t max_abs = 1;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    uint16_t v = saturating_abs16(src[i]);
    if (v > max_abs)
      max_abs = v;
  }
  int exponent = bfp_exponent(max_abs, iq_bits);
  dst[0] = (int8_t)exponent;
  int32_t vals[FH_VALS_PER_PRB];
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    vals[i] = src[i] >> exponent;
  pack_prb((uint8_t *)(dst + 1), vals, iq_bits);
}

// Vector BFP, iq_bits <= 14. The PRB's 24 values are one 256-bit and one 128-bit register:
//  - exponent: saturating |x| (like saturating_abs16()), vertical max, then a horizontal max
//    through minpos of the complement;
//  - mantissas: arithmetic shift by the exponent, masked to iq_bits;
//  - packing: madd merges each value pair into a 32-bit lane (v0 << w | v1, both factors fit
//    int16 because w <= 14), a 64-bit shift/or merges lane pairs (4 values, 4w bits), and each
//    two such lanes form one 8-value group of w bytes, stored big-endian.
FH_INLINE void bfp_compress_prb_simd(const int16_t *src, int8_t *dst, int w)
{
  const simde__m256i lo = simde_mm256_loadu_si256((const simde__m256i *)src);
  const simde__m128i hi = simde_mm_loadu_si128((const simde__m128i *)(src + 16));

  const simde__m256i sat = simde_mm256_set1_epi16(INT16_MAX);
  const simde__m256i abs_lo = simde_mm256_min_epu16(simde_mm256_abs_epi16(lo), sat);
  const simde__m128i abs_hi = simde_mm_min_epu16(simde_mm_abs_epi16(hi), simde_mm256_castsi256_si128(sat));
  simde__m128i m = simde_mm_max_epu16(simde_mm256_castsi256_si128(abs_lo), simde_mm256_extracti128_si256(abs_lo, 1));
  m = simde_mm_max_epu16(m, abs_hi);
  const simde__m128i inv_min = simde_mm_minpos_epu16(simde_mm_xor_si128(m, simde_mm_set1_epi16(-1)));
  const uint16_t max_abs = (uint16_t)~simde_mm_extract_epi16(inv_min, 0);
  const int exponent = bfp_exponent(max_abs, w);
  dst[0] = (int8_t)exponent;

  const simde__m128i exp_cnt = simde_mm_cvtsi32_si128(exponent);
  const simde__m128i pair_cnt = simde_mm_cvtsi32_si128(2 * w);
  const simde__m256i mask = simde_mm256_set1_epi16((int16_t)((1 << w) - 1));
  const simde__m256i pair_mul = simde_mm256_set1_epi32((1 << 16) | (1 << w)); // even value * 2^w + odd value * 1
  const simde__m256i low32 = simde_mm256_set1_epi64x(0xFFFFFFFF);

  simde__m256i v_lo = simde_mm256_and_si256(simde_mm256_sra_epi16(lo, exp_cnt), mask);
  simde__m256i p_lo = simde_mm256_madd_epi16(v_lo, pair_mul);
  simde__m256i q_lo =
      simde_mm256_or_si256(simde_mm256_sll_epi64(simde_mm256_and_si256(p_lo, low32), pair_cnt), simde_mm256_srli_epi64(p_lo, 32));
  simde__m128i v_hi = simde_mm_and_si128(simde_mm_sra_epi16(hi, exp_cnt), simde_mm256_castsi256_si128(mask));
  simde__m128i p_hi = simde_mm_madd_epi16(v_hi, simde_mm256_castsi256_si128(pair_mul));
  simde__m128i q_hi = simde_mm_or_si128(simde_mm_sll_epi64(simde_mm_and_si128(p_hi, simde_mm256_castsi256_si128(low32)), pair_cnt),
                                        simde_mm_srli_epi64(p_hi, 32));

  uint64_t quads[6]; // each holds 4 values (4w bits), in order
  simde_mm256_storeu_si256((simde__m256i *)quads, q_lo);
  simde_mm_storeu_si128((simde__m128i *)(quads + 4), q_hi);
  uint8_t *out = (uint8_t *)(dst + 1);
  for (int g = 0; g < 3; g++)
    put_group_be(out + g * w, ((unsigned __int128)quads[2 * g] << (4 * w)) | quads[2 * g + 1], w);
}

FH_INLINE void bfp_compress_prb(const int16_t *src, int8_t *dst, int iq_bits)
{
  if (iq_bits <= 14)
    bfp_compress_prb_simd(src, dst, iq_bits);
  else
    bfp_compress_prb_scalar(src, dst, iq_bits);
}

// Scalar BFP decompression for any width: one 8-value group (w bytes) at a time, each value
// sign-extended from bit w - 1 and scaled back up.
FH_INLINE void bfp_decompress_prb_scalar(const int8_t *src, int16_t *dst, int w)
{
  const int exponent = (int)(uint8_t)src[0];
  const uint8_t *in = (const uint8_t *)(src + 1);
  for (int g = 0; g < 3; g++) {
    const unsigned __int128 group = get_group_be(in + g * w, w);
    for (int i = 0; i < 8; i++) {
      const uint64_t raw = (uint64_t)(group >> (w * (7 - i)));
      const int32_t v = (int32_t)((int64_t)(raw << (64 - w)) >> (64 - w));
      dst[8 * g + i] = (int16_t)((uint32_t)v << exponent);
    }
  }
}

// Per-width constants for the vector decompressor: for each of a group's 8 values, the byte
// shuffle that puts the (up to) 3 bytes holding it big-endian into the top of a 32-bit lane,
// and the left shift that then brings its first bit to bit 31.
typedef struct {
  simde__m256i gather;
  simde__m256i align;
  simde__m128i sign_cnt;
} bfp_unpack_ctl_t;

static bfp_unpack_ctl_t bfp_unpack_ctl(int w)
{
  uint8_t gather[32];
  uint32_t align[8];
  for (int i = 0; i < 8; i++) {
    const int bit = i * w;
    // Both 128-bit halves hold the same group bytes, so indices are relative to the group.
    gather[4 * i + 0] = 0x80; // zero
    gather[4 * i + 1] = (uint8_t)(bit / 8 + 2);
    gather[4 * i + 2] = (uint8_t)(bit / 8 + 1);
    gather[4 * i + 3] = (uint8_t)(bit / 8);
    align[i] = bit % 8;
  }
  bfp_unpack_ctl_t ctl;
  ctl.gather = simde_mm256_loadu_si256((const simde__m256i *)gather);
  ctl.align = simde_mm256_loadu_si256((const simde__m256i *)align);
  ctl.sign_cnt = simde_mm_cvtsi32_si128(32 - w);
  return ctl;
}

// One group of 8 values -> 8 sign-extended 32-bit lanes. Reads 16 bytes from `in` (w of them
// are the group), so the caller must guarantee 16 readable bytes.
FH_INLINE simde__m256i bfp_unpack_group(const uint8_t *in, const bfp_unpack_ctl_t *ctl)
{
  const simde__m256i bytes = simde_mm256_broadcastsi128_si256(simde_mm_loadu_si128((const simde__m128i *)in));
  const simde__m256i lanes = simde_mm256_sllv_epi32(simde_mm256_shuffle_epi8(bytes, ctl->gather), ctl->align);
  return simde_mm256_sra_epi32(lanes, ctl->sign_cnt);
}

// Vector BFP decompression. The last group of a PRB reads past its end (up to 16 - w bytes),
// so this is only used when at least 16 - w more bytes follow the PRB.
FH_INLINE void bfp_decompress_prb_simd(const int8_t *src, int16_t *dst, int w, const bfp_unpack_ctl_t *ctl)
{
  const simde__m128i exp_cnt = simde_mm_cvtsi32_si128((uint8_t)src[0]);
  const uint8_t *in = (const uint8_t *)(src + 1);
  // Truncate to 16 bits like the scalar path's (int16_t) cast: mask, then an unsigned pack that
  // can no longer saturate. packus interleaves the 128-bit halves, permute4x64 restores order.
  const simde__m256i low16 = simde_mm256_set1_epi32(0xFFFF);
  const simde__m256i g0 = simde_mm256_and_si256(bfp_unpack_group(in, ctl), low16);
  const simde__m256i g1 = simde_mm256_and_si256(bfp_unpack_group(in + w, ctl), low16);
  const simde__m256i g2 = simde_mm256_and_si256(bfp_unpack_group(in + 2 * w, ctl), low16);
  const simde__m256i v01 = simde_mm256_permute4x64_epi64(simde_mm256_packus_epi32(g0, g1), 0xD8);
  const simde__m256i v22 = simde_mm256_permute4x64_epi64(simde_mm256_packus_epi32(g2, g2), 0xD8);
  simde_mm256_storeu_si256((simde__m256i *)dst, simde_mm256_sll_epi16(v01, exp_cnt));
  simde_mm_storeu_si128((simde__m128i *)(dst + 16), simde_mm_sll_epi16(simde_mm256_castsi256_si128(v22), exp_cnt));
}

// Per-call BFP loops. The switch hands bfp_compress_prb()/bfp_decompress_prb() a compile-time
// width for the widths O-RAN deployments use, so each gets its own unrolled kernel; any other
// width takes the generic (runtime-width) path.
#define FH_BFP_WIDTH_CASES(X) X(8) X(9) X(10) X(12) X(14) X(16)

static void bfp_compress_prbs(int iq_bits, int n_prb, const int16_t *src, int8_t *dst)
{
  const int dst_stride = FH_COMP_PRB_BYTES(iq_bits);
  switch (iq_bits) {
#define X(w)                                                                \
  case w:                                                                   \
    for (int p = 0; p < n_prb; p++)                                         \
      bfp_compress_prb(src + p * FH_VALS_PER_PRB, dst + p * dst_stride, w); \
    break;
    FH_BFP_WIDTH_CASES(X)
#undef X
    default:
      for (int p = 0; p < n_prb; p++)
        bfp_compress_prb(src + p * FH_VALS_PER_PRB, dst + p * dst_stride, iq_bits);
  }
}

static void bfp_decompress_prbs(int iq_bits, int n_prb, const int8_t *src, int16_t *dst)
{
  const int src_stride = FH_COMP_PRB_BYTES(iq_bits);
  // The vector path reads 16 bytes from the PRB's last group (offset 1 + 2w): up to 16 - w bytes
  // past its end, which stays inside the next PRB (1 + 3w bytes) for w >= 4. So every PRB but the
  // last one takes it, at the widths where that holds.
  const bfp_unpack_ctl_t ctl = bfp_unpack_ctl(iq_bits);
  int p = 0;
  for (; iq_bits >= 4 && p < n_prb - 1; p++)
    bfp_decompress_prb_simd(src + p * src_stride, dst + p * FH_VALS_PER_PRB, iq_bits, &ctl);
  for (; p < n_prb; p++)
    bfp_decompress_prb_scalar(src + p * src_stride, dst + p * FH_VALS_PER_PRB, iq_bits);
}

/* ---- BLKSCALE ---- */

static void blkscale_compress_prb(const int16_t *src, int8_t *dst, int iq_bits)
{
  int32_t max_abs = 1;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    int32_t v = src[i] < 0 ? -(int32_t)src[i] : (int32_t)src[i];
    if (v > max_abs)
      max_abs = v;
  }
  int bit_width = 32 - __builtin_clz((uint32_t)max_abs);
  int shift = bit_width - (iq_bits - 1);
  if (shift < 0)
    shift = 0;
  dst[0] = (int8_t)shift;
  int32_t vals[FH_VALS_PER_PRB];
  const int32_t clip_max = (1 << (iq_bits - 1)) - 1;
  const int32_t clip_min = -(1 << (iq_bits - 1));
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    int32_t val = src[i] >> shift;
    if (val > clip_max)
      val = clip_max;
    else if (val < clip_min)
      val = clip_min;
    vals[i] = val;
  }
  pack_prb((uint8_t *)(dst + 1), vals, iq_bits);
}

static void blkscale_decompress_prb(const int8_t *src, int16_t *dst, int iq_bits)
{
  int shift = (int)(uint8_t)src[0];
  int32_t vals[FH_VALS_PER_PRB];
  unpack_prb((const uint8_t *)(src + 1), vals, iq_bits);
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    dst[i] = (int16_t)((uint32_t)vals[i] << shift);
}

/* ---- ULAW ---- */

/* G.711 mu-law constants (ITU-T G.711) */
#define ULAW_BIAS 33 /* additive bias before segmentation */
#define ULAW_CLIP 32734 /* saturation level: INT16_MAX - ULAW_BIAS */
#define ULAW_NORM 127 /* segment normalization factor */
#define ULAW_SEG_MASK 0x07 /* 3-bit segment index */

static int16_t ulaw_encode(int16_t s, int iq_bits)
{
  int sign = s < 0 ? -1 : 1;
  int x = s < 0 ? -(int)s : (int)s;
  x = x > ULAW_CLIP ? ULAW_CLIP : x;
  x += ULAW_BIAS;
  int seg = 0;
  for (int t = x >> 5; t > 1 && seg < 7; t >>= 1)
    seg++;
  int code = (((x >> (seg + 1)) & 0x0F) | (seg << 4)) ^ 0x7F;
  int peak = (1 << (iq_bits - 1)) - 1;
  return (int16_t)(sign * (code * peak / ULAW_NORM));
}

static int16_t ulaw_decode(int16_t s, int iq_bits)
{
  int sign = s < 0 ? -1 : 1;
  int x = s < 0 ? -(int)s : (int)s;
  int peak = (1 << (iq_bits - 1)) - 1;
  if (peak == 0)
    return 0;
  int code = x * ULAW_NORM / peak;
  code ^= 0x7F;
  int seg = (code >> 4) & ULAW_SEG_MASK;
  int decoded = (((code & 0x0F) << 1) | 1) << (seg + 2);
  decoded -= ULAW_BIAS;
  if (decoded < 0)
    decoded = 0;
  return (int16_t)(sign * decoded);
}

static void ulaw_compress_prb(const int16_t *src, int8_t *dst, int iq_bits)
{
  dst[0] = 0;
  int32_t vals[FH_VALS_PER_PRB];
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    vals[i] = ulaw_encode(src[i], iq_bits);
  pack_prb((uint8_t *)(dst + 1), vals, iq_bits);
}

static void ulaw_decompress_prb(const int8_t *src, int16_t *dst, int iq_bits)
{
  int32_t vals[FH_VALS_PER_PRB];
  unpack_prb((const uint8_t *)(src + 1), vals, iq_bits);
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    dst[i] = ulaw_decode((int16_t)vals[i], iq_bits);
}

/* ---- public API ---- */

void fh_compress_prbs(fh_comp_method_t method, int iq_bits, int n_prb, const int16_t *src, int8_t *dst)
{
  AssertFatal(method != FH_COMP_NONE, "fh_compress_prbs called with FH_COMP_NONE\n");
  AssertFatal(iq_bits >= 1 && iq_bits <= 16, "iq_bits %d out of range [1..16]\n", iq_bits);
  if (method == FH_COMP_BFP) {
    bfp_compress_prbs(iq_bits, n_prb, src, dst);
    return;
  }
  const int src_stride = FH_VALS_PER_PRB;
  const int dst_stride = FH_COMP_PRB_BYTES(iq_bits);
  for (int p = 0; p < n_prb; p++) {
    switch (method) {
      case FH_COMP_BLKSCALE:
        blkscale_compress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      case FH_COMP_ULAW:
        ulaw_compress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      default:
        AssertFatal(0, "Unsupported compression method %d\n", method);
    }
  }
}

void fh_decompress_prbs(fh_comp_method_t method, int iq_bits, int n_prb, const int8_t *src, int16_t *dst)
{
  AssertFatal(method != FH_COMP_NONE, "fh_decompress_prbs called with FH_COMP_NONE\n");
  AssertFatal(iq_bits >= 1 && iq_bits <= 16, "iq_bits %d out of range [1..16]\n", iq_bits);
  if (method == FH_COMP_BFP) {
    bfp_decompress_prbs(iq_bits, n_prb, src, dst);
    return;
  }
  const int src_stride = FH_COMP_PRB_BYTES(iq_bits);
  const int dst_stride = FH_VALS_PER_PRB;
  for (int p = 0; p < n_prb; p++) {
    switch (method) {
      case FH_COMP_BLKSCALE:
        blkscale_decompress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      case FH_COMP_ULAW:
        ulaw_decompress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      default:
        AssertFatal(0, "Unsupported compression method %d\n", method);
    }
  }
}

void fh_compress_prach(fh_comp_method_t method, int iq_bits, int kbar, const int16_t *src, int8_t *dst)
{
  const int total_vals = FH_PRACH_NUM_PRBS * FH_VALS_PER_PRB;
  int16_t padded[FH_PRACH_NUM_PRBS * FH_VALS_PER_PRB];
  AssertFatal(kbar >= 0 && kbar + FH_PRACH_NUM_SUBCARRIERS * 2 <= total_vals, "PRACH kbar %d out of range\n", kbar);
  memset(padded, 0, total_vals * sizeof(int16_t));
  memcpy(padded + kbar, src, FH_PRACH_NUM_SUBCARRIERS * 2 * sizeof(int16_t));
  fh_compress_prbs(method, iq_bits, FH_PRACH_NUM_PRBS, padded, dst);
}
