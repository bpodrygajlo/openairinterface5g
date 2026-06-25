/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <uhd/convert.hpp>
#include <string>
#include "openair1/PHY/sse_intrin.h"
#include "usrp_converters.hpp"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
constexpr bool HOST_IS_BIG_ENDIAN = true;
#else
constexpr bool HOST_IS_BIG_ENDIAN = false;
#endif

const std::string CPU_FORMAT_OAI = "sc16_oai";

namespace uhd { namespace convert {
  converter::~converter(void) {
    // NOP
  }
}}

template <int Shift, bool SwapBytes>
class sc16_oai_rx_converter : public uhd::convert::converter {
public:
  virtual ~sc16_oai_rx_converter(void) override {}

  void set_scalar(const double) override {
    // No-op
  }

  void operator()(const input_type& in, const output_type& out, const size_t num) override {
    for (size_t chan = 0; chan < in.size(); ++chan) {
      const int16_t* src = reinterpret_cast<const int16_t*>(in[chan]);
      int16_t* dest = reinterpret_cast<int16_t*>(out[chan]);
      
      size_t total_ints = num * 2;
      size_t j = 0;

#if defined(__AVX512F__) && defined(__AVX512BW__)
      bool src_aligned = ((((uintptr_t)src) & 0x3F) == 0);
      bool dest_aligned = ((((uintptr_t)dest) & 0x3F) == 0);
      __m512i mask_ff = _mm512_set1_epi16(0x00FF);

      if (src_aligned && dest_aligned) {
        for (; j + 31 < total_ints; j += 32) {
          __m512i v_in = _mm512_load_si512(reinterpret_cast<const __m512i*>(&src[j]));
          if (SwapBytes) {
            __m512i low = _mm512_and_si512(_mm512_srli_epi16(v_in, 8), mask_ff);
            __m512i high = _mm512_slli_epi16(v_in, 8);
            v_in = _mm512_or_si512(low, high);
          }
          __m512i v_out = _mm512_srai_epi16(v_in, Shift);
          _mm512_store_si512(reinterpret_cast<__m512i*>(&dest[j]), v_out);
        }
      } else {
        for (; j + 31 < total_ints; j += 32) {
          __m512i v_in = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&src[j]));
          if (SwapBytes) {
            __m512i low = _mm512_and_si512(_mm512_srli_epi16(v_in, 8), mask_ff);
            __m512i high = _mm512_slli_epi16(v_in, 8);
            v_in = _mm512_or_si512(low, high);
          }
          __m512i v_out = _mm512_srai_epi16(v_in, Shift);
          _mm512_storeu_si512(reinterpret_cast<__m512i*>(&dest[j]), v_out);
        }
      }
#elif defined(__AVX2__)
      bool src_aligned = ((((uintptr_t)src) & 0x1F) == 0);
      bool dest_aligned = ((((uintptr_t)dest) & 0x1F) == 0);
      simde__m256i mask_ff = simde_mm256_set1_epi16(0x00FF);

      if (src_aligned && dest_aligned) {
        for (; j + 15 < total_ints; j += 16) {
          simde__m256i v_in = simde_mm256_load_si256(reinterpret_cast<const simde__m256i*>(&src[j]));
          if (SwapBytes) {
            simde__m256i low = simde_mm256_and_si256(simde_mm256_srli_epi16(v_in, 8), mask_ff);
            simde__m256i high = simde_mm256_slli_epi16(v_in, 8);
            v_in = simde_mm256_or_si256(low, high);
          }
          simde__m256i v_out = simde_mm256_srai_epi16(v_in, Shift);
          simde_mm256_store_si256(reinterpret_cast<simde__m256i*>(&dest[j]), v_out);
        }
      } else {
        for (; j + 15 < total_ints; j += 16) {
          simde__m256i v_in = simde_mm256_loadu_si256(reinterpret_cast<const simde__m256i*>(&src[j]));
          if (SwapBytes) {
            simde__m256i low = simde_mm256_and_si256(simde_mm256_srli_epi16(v_in, 8), mask_ff);
            simde__m256i high = simde_mm256_slli_epi16(v_in, 8);
            v_in = simde_mm256_or_si256(low, high);
          }
          simde__m256i v_out = simde_mm256_srai_epi16(v_in, Shift);
          simde_mm256_storeu_si256(reinterpret_cast<simde__m256i*>(&dest[j]), v_out);
        }
      }
#elif defined(__SSE2__)
      bool src_aligned = ((((uintptr_t)src) & 0x0F) == 0);
      bool dest_aligned = ((((uintptr_t)dest) & 0x0F) == 0);
      simde__m128i mask_ff = simde_mm_set1_epi16(0x00FF);

      if (src_aligned && dest_aligned) {
        for (; j + 7 < total_ints; j += 8) {
          simde__m128i v_in = simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(&src[j]));
          if (SwapBytes) {
            simde__m128i low = simde_mm_and_si128(simde_mm_srli_epi16(v_in, 8), mask_ff);
            simde__m128i high = simde_mm_slli_epi16(v_in, 8);
            v_in = simde_mm_or_si128(low, high);
          }
          simde__m128i v_out = simde_mm_srai_epi16(v_in, Shift);
          simde_mm_store_si128(reinterpret_cast<simde__m128i*>(&dest[j]), v_out);
        }
      } else {
        for (; j + 7 < total_ints; j += 8) {
          simde__m128i v_in = simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(&src[j]));
          if (SwapBytes) {
            simde__m128i low = simde_mm_and_si128(simde_mm_srli_epi16(v_in, 8), mask_ff);
            simde__m128i high = simde_mm_slli_epi16(v_in, 8);
            v_in = simde_mm_or_si128(low, high);
          }
          simde__m128i v_out = simde_mm_srai_epi16(v_in, Shift);
          simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(&dest[j]), v_out);
        }
      }
#endif
      for (; j < total_ints; ++j) {
        int16_t val = src[j];
        if (SwapBytes) {
          val = (val << 8) | ((val >> 8) & 0x00FF);
        }
        dest[j] = val >> Shift;
      }
    }
  }
};

template <int Shift, bool SwapBytes>
class sc16_oai_tx_converter : public uhd::convert::converter {
public:
  virtual ~sc16_oai_tx_converter(void) override {}

  void set_scalar(const double) override {
    // No-op
  }

  void operator()(const input_type& in, const output_type& out, const size_t num) override {
    for (size_t chan = 0; chan < in.size(); ++chan) {
      const int16_t* src = reinterpret_cast<const int16_t*>(in[chan]);
      int16_t* dest = reinterpret_cast<int16_t*>(out[chan]);
      
      size_t total_ints = num * 2;
      size_t j = 0;

#if defined(__AVX512F__) && defined(__AVX512BW__)
      bool src_aligned = ((((uintptr_t)src) & 0x3F) == 0);
      bool dest_aligned = ((((uintptr_t)dest) & 0x3F) == 0);
      __m512i mask_ff = _mm512_set1_epi16(0x00FF);

      if (src_aligned && dest_aligned) {
        for (; j + 31 < total_ints; j += 32) {
          __m512i v_in = _mm512_load_si512(reinterpret_cast<const __m512i*>(&src[j]));
          __m512i v_out = _mm512_slli_epi16(v_in, Shift);
          if (SwapBytes) {
            __m512i low = _mm512_and_si512(_mm512_srli_epi16(v_out, 8), mask_ff);
            __m512i high = _mm512_slli_epi16(v_out, 8);
            v_out = _mm512_or_si512(low, high);
          }
          _mm512_store_si512(reinterpret_cast<__m512i*>(&dest[j]), v_out);
        }
      } else {
        for (; j + 31 < total_ints; j += 32) {
          __m512i v_in = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&src[j]));
          __m512i v_out = _mm512_slli_epi16(v_in, Shift);
          if (SwapBytes) {
            __m512i low = _mm512_and_si512(_mm512_srli_epi16(v_out, 8), mask_ff);
            __m512i high = _mm512_slli_epi16(v_out, 8);
            v_out = _mm512_or_si512(low, high);
          }
          _mm512_storeu_si512(reinterpret_cast<__m512i*>(&dest[j]), v_out);
        }
      }
#elif defined(__AVX2__)
      bool src_aligned = ((((uintptr_t)src) & 0x1F) == 0);
      bool dest_aligned = ((((uintptr_t)dest) & 0x1F) == 0);
      simde__m256i mask_ff = simde_mm256_set1_epi16(0x00FF);

      if (src_aligned && dest_aligned) {
        for (; j + 15 < total_ints; j += 16) {
          simde__m256i v_in = simde_mm256_load_si256(reinterpret_cast<const simde__m256i*>(&src[j]));
          simde__m256i v_out = simde_mm256_slli_epi16(v_in, Shift);
          if (SwapBytes) {
            simde__m256i low = simde_mm256_and_si256(simde_mm256_srli_epi16(v_out, 8), mask_ff);
            simde__m256i high = simde_mm256_slli_epi16(v_out, 8);
            v_out = simde_mm256_or_si256(low, high);
          }
          simde_mm256_store_si256(reinterpret_cast<simde__m256i*>(&dest[j]), v_out);
        }
      } else {
        for (; j + 15 < total_ints; j += 16) {
          simde__m256i v_in = simde_mm256_loadu_si256(reinterpret_cast<const simde__m256i*>(&src[j]));
          simde__m256i v_out = simde_mm256_slli_epi16(v_in, Shift);
          if (SwapBytes) {
            simde__m256i low = simde_mm256_and_si256(simde_mm256_srli_epi16(v_out, 8), mask_ff);
            simde__m256i high = simde_mm256_slli_epi16(v_out, 8);
            v_out = simde_mm256_or_si256(low, high);
          }
          simde_mm256_storeu_si256(reinterpret_cast<simde__m256i*>(&dest[j]), v_out);
        }
      }
#elif defined(__SSE2__)
      bool src_aligned = ((((uintptr_t)src) & 0x0F) == 0);
      bool dest_aligned = ((((uintptr_t)dest) & 0x0F) == 0);
      simde__m128i mask_ff = simde_mm_set1_epi16(0x00FF);

      if (src_aligned && dest_aligned) {
        for (; j + 7 < total_ints; j += 8) {
          simde__m128i v_in = simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(&src[j]));
          simde__m128i v_out = simde_mm_slli_epi16(v_in, Shift);
          if (SwapBytes) {
            simde__m128i low = simde_mm_and_si128(simde_mm_srli_epi16(v_out, 8), mask_ff);
            simde__m128i high = simde_mm_slli_epi16(v_out, 8);
            v_out = simde_mm_or_si128(low, high);
          }
          simde_mm_store_si128(reinterpret_cast<simde__m128i*>(&dest[j]), v_out);
        }
      } else {
        for (; j + 7 < total_ints; j += 8) {
          simde__m128i v_in = simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(&src[j]));
          simde__m128i v_out = simde_mm_slli_epi16(v_in, Shift);
          if (SwapBytes) {
            simde__m128i low = simde_mm_and_si128(simde_mm_srli_epi16(v_out, 8), mask_ff);
            simde__m128i high = simde_mm_slli_epi16(v_out, 8);
            v_out = simde_mm_or_si128(low, high);
          }
          simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(&dest[j]), v_out);
        }
      }
#endif
      for (; j < total_ints; ++j) {
        int16_t val = src[j] << Shift;
        if (SwapBytes) {
          val = (val << 8) | ((val >> 8) & 0x00FF);
        }
        dest[j] = val;
      }
    }
  }
};

void register_oai_converters(int rxshift) {
  uhd::convert::register_bytes_per_item(CPU_FORMAT_OAI, sizeof(int16_t) * 2);

  constexpr bool swap_le = (false != HOST_IS_BIG_ENDIAN);
  constexpr bool swap_be = (true != HOST_IS_BIG_ENDIAN);

  for (size_t num_chans = 1; num_chans <= 4; ++num_chans) {
    // 1. LE wire format
    {
      uhd::convert::id_type rx_id;
      rx_id.input_format = "sc16_item32_le";
      rx_id.num_inputs = num_chans;
      rx_id.output_format = CPU_FORMAT_OAI;
      rx_id.num_outputs = num_chans;

      switch (rxshift) {
        case 2:
          uhd::convert::register_converter(
            rx_id,
            []() { return uhd::convert::converter::sptr(new sc16_oai_rx_converter<2, swap_le>()); },
            100
          );
          break;
        case 4:
          uhd::convert::register_converter(
            rx_id,
            []() { return uhd::convert::converter::sptr(new sc16_oai_rx_converter<4, swap_le>()); },
            100
          );
          break;
        default:
          break;
      }

      uhd::convert::id_type tx_id;
      tx_id.input_format = CPU_FORMAT_OAI;
      tx_id.num_inputs = num_chans;
      tx_id.output_format = rx_id.input_format;
      tx_id.num_outputs = num_chans;

      uhd::convert::register_converter(
        tx_id,
        []() { return uhd::convert::converter::sptr(new sc16_oai_tx_converter<4, swap_le>()); },
        100
      );
    }

    // 2. BE wire format
    {
      uhd::convert::id_type rx_id;
      rx_id.input_format = "sc16_item32_be";
      rx_id.num_inputs = num_chans;
      rx_id.output_format = CPU_FORMAT_OAI;
      rx_id.num_outputs = num_chans;

      switch (rxshift) {
        case 2:
          uhd::convert::register_converter(
            rx_id,
            []() { return uhd::convert::converter::sptr(new sc16_oai_rx_converter<2, swap_be>()); },
            100
          );
          break;
        case 4:
          uhd::convert::register_converter(
            rx_id,
            []() { return uhd::convert::converter::sptr(new sc16_oai_rx_converter<4, swap_be>()); },
            100
          );
          break;
        default:
          break;
      }

      uhd::convert::id_type tx_id;
      tx_id.input_format = CPU_FORMAT_OAI;
      tx_id.num_inputs = num_chans;
      tx_id.output_format = rx_id.input_format;
      tx_id.num_outputs = num_chans;

      uhd::convert::register_converter(
        tx_id,
        []() { return uhd::convert::converter::sptr(new sc16_oai_tx_converter<4, swap_be>()); },
        100
      );
    }
  }
}
