/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdint.h>
#include "openair1/PHY/TOOLS/tools_defs.h"

// Undefine macro 'T' from common/utils/T/T.h to prevent macro collision with Highway template parameter names.
#undef T
#include <hwy/highway.h>

#if HWY_ARCH_ARM_A64 && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>

// Hand-written SVE2 intrinsics implementation
static inline void rotate_cpx_vector_sve2_shift15(const c16_t *const x, const c16_t alpha, c16_t *y, uint32_t N)
{
  namespace hn = hwy::HWY_NAMESPACE;
  const hn::ScalableTag<int16_t> d16;
  const size_t lanes = hn::Lanes(d16);
  size_t i = 0;
  for (; i + 2 * lanes <= 2 * N; i += 2 * lanes) {
    svint16_t sv_x0 = svld1_s16(svptrue_b16(), reinterpret_cast<const int16_t*>(x) + i);
    svint16_t sv_x1 = svld1_s16(svptrue_b16(), reinterpret_cast<const int16_t*>(x) + i + lanes);

    svint16_t sv_ar = svdup_n_s16(alpha.r);
    svint16_t sv_ai = svdup_n_s16(alpha.i);

    svint16_t sv_br = svuzp1_s16(sv_x0, sv_x1);
    svint16_t sv_bi = svuzp2_s16(sv_x0, sv_x1);

    svint16_t sv_real = svqdmulh_s16(sv_ar, sv_br);
    svint16_t sv_imag = svqdmulh_s16(sv_ar, sv_bi);

    sv_real = svqrdmlsh_s16(sv_real, sv_ai, sv_bi);
    sv_imag = svqrdmlah_s16(sv_imag, sv_ai, sv_br);

    svint16_t sv_y0 = svzip1_s16(sv_real, sv_imag);
    svint16_t sv_y1 = svzip2_s16(sv_real, sv_imag);

    svst1_s16(svptrue_b16(), reinterpret_cast<int16_t*>(y) + i, sv_y0);
    svst1_s16(svptrue_b16(), reinterpret_cast<int16_t*>(y) + i + lanes, sv_y1);
  }

  // Fallback for remaining elements (SVE2 specific tail)
  for (size_t k = i / 2; k < N; ++k) {
    int32_t prod_r1 = x[k].r * alpha.r;
    int32_t prod_r2 = x[k].i * alpha.i;
    int32_t prod_i1 = x[k].r * alpha.i;
    int32_t prod_i2 = x[k].i * alpha.r;

    auto saturate_double_prod = [](int64_t v) -> int32_t {
      if (v > 2147483647) return 2147483647;
      if (v < -2147483648) return -2147483648;
      return (int32_t)v;
    };

    int32_t dp_r1 = saturate_double_prod(2LL * prod_r1);
    int32_t dp_r2 = saturate_double_prod(2LL * prod_r2);
    int32_t dp_i1 = saturate_double_prod(2LL * prod_i1);
    int32_t dp_i2 = saturate_double_prod(2LL * prod_i2);

    int32_t r1 = dp_r1 >> 16;
    int32_t r2 = (dp_r2 + 32768) >> 16;
    int32_t i1 = (dp_i1 + 32768) >> 16;
    int32_t i2 = dp_i2 >> 16;

    int32_t r_final = r1 - r2;
    int32_t im_final = i1 + i2;

    y[k].r = r_final > 32767 ? 32767 : (r_final < -32768 ? -32768 : r_final);
    y[k].i = im_final > 32767 ? 32767 : (im_final < -32768 ? -32768 : im_final);
  }
}
#endif

// Google Highway implementation
static inline void rotate_cpx_vector_generic(const c16_t *const x, const c16_t alpha, c16_t *y, uint32_t N, uint16_t output_shift)
{
  namespace hn = hwy::HWY_NAMESPACE;
  const hn::ScalableTag<int16_t> d16;
  const hn::ScalableTag<int32_t> d32;

  const int32_t val_real = (static_cast<uint16_t>(alpha.r)) | (static_cast<uint32_t>(static_cast<uint16_t>(-alpha.i)) << 16);
  const int32_t val_imag = (static_cast<uint16_t>(alpha.i)) | (static_cast<uint32_t>(static_cast<uint16_t>(alpha.r)) << 16);

  const auto valpha_real = hn::BitCast(d16, hn::Set(d32, val_real));
  const auto valpha_imag = hn::BitCast(d16, hn::Set(d32, val_imag));

  const size_t lanes = hn::Lanes(d16);
  size_t i = 0;
  for (; i + 2 * lanes <= 2 * N; i += 2 * lanes) {
    const auto vx0 = hn::LoadU(d16, reinterpret_cast<const int16_t*>(x) + i);
    const auto vx1 = hn::LoadU(d16, reinterpret_cast<const int16_t*>(x) + i + lanes);

    auto p_real0 = hn::WidenMulPairwiseAdd(d32, vx0, valpha_real);
    auto p_imag0 = hn::WidenMulPairwiseAdd(d32, vx0, valpha_imag);
    auto p_real1 = hn::WidenMulPairwiseAdd(d32, vx1, valpha_real);
    auto p_imag1 = hn::WidenMulPairwiseAdd(d32, vx1, valpha_imag);

    p_real0 = hn::ShiftRightSame(p_real0, output_shift);
    p_imag0 = hn::ShiftRightSame(p_imag0, output_shift);
    p_real1 = hn::ShiftRightSame(p_real1, output_shift);
    p_imag1 = hn::ShiftRightSame(p_imag1, output_shift);

    const auto y_re = hn::OrderedDemote2To(d16, p_real0, p_real1);
    const auto y_im = hn::OrderedDemote2To(d16, p_imag0, p_imag1);

    hn::StoreInterleaved2(y_re, y_im, d16, reinterpret_cast<int16_t*>(y) + i);
  }

  // Fallback for remaining elements
  for (size_t k = i / 2; k < N; ++k) {
    int32_t r = (x[k].r * alpha.r - x[k].i * alpha.i);
    int32_t im = (x[k].r * alpha.i + x[k].i * alpha.r);
    r >>= output_shift;
    im >>= output_shift;
    y[k].r = r > 32767 ? 32767 : (r < -32768 ? -32768 : r);
    y[k].i = im > 32767 ? 32767 : (im < -32768 ? -32768 : im);
  }
}

// C-compliant dynamic shift function API
extern "C" void rotate_cpx_vector(const c16_t *const x, const c16_t alpha, c16_t *y, uint32_t N, uint16_t output_shift)
{
#if HWY_ARCH_ARM_A64 && defined(__ARM_FEATURE_SVE)
  if (output_shift == 15) {
    rotate_cpx_vector_sve2_shift15(x, alpha, y, N);
    return;
  }
#endif
  rotate_cpx_vector_generic(x, alpha, y, N, output_shift);
}
