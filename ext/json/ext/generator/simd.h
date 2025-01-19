#include "extconf.h"

#ifdef ENABLE_SIMD

#ifdef HAVE_ARM_NEON_H
#include <arm_neon.h>

#ifdef HAVE_TYPE_UINT8X16_T

#define SIMD_VEC_STRIDE          16

#define simd_vec_type            uint8x16_t
#define simd_vec_from_byte       vdupq_n_u8
#define simd_vec_load_from_mem   vld1q_u8
#define simd_vec_to_memory       vst1q_u8
#define simd_vec_eq              vceqq_u8
#define simd_vec_lt              vcltq_u8
#define simd_vec_gt              vcgtq_u8
#define simd_vec_or              vorrq_u8
#define simd_vec_and             vandq_u8
#define simd_vec_max             vmaxvq_u8

inline int simd_vec_any_set(uint8x16_t vec) {
    return vmaxvq_u8(vec) != 0;
}

inline int simd_vec_all_zero(uint8x16_t vec) {
    return vmaxvq_u8(vec) == 0;
}

#elif HAVE_TYPE_UINT8X8_T

#define SIMD_VEC_STRIDE          8
#define simd_vec_type            uint8x8_t
#define simd_vec_from_byte       vdup_n_u8
#define simd_vec_load_from_mem   vld1_u8
#define simd_vec_to_memory       vst1_u8
#define simd_vec_eq              vceq_u8
#define simd_vec_lt              vclt_u8
#define simd_vec_gt              vcgt_u8
#define simd_vec_or              vorr_u8
#define simd_vec_and             vand_u8
#define simd_vec_max             vmaxv_u8

inline int smd_vec_any_set(uint8x8_t vec) {
    return vmaxv_u8(vec) != 0;
}

inline int smd_vec_all_zero(uint8x8_t vec) {
    return vmaxv_u8(vec) == 0;
}

#endif /* HAVE_TYPE_UINT8X16_T */
#endif /* HAVE_ARM_NEON_H */

#ifdef HAVE_X86INTRIN_H
#include <x86intrin.h>

#ifdef HAVE_TYPE___M256I

#define SIMD_VEC_STRIDE             32

#define _mm256_cmpge_epu8(a, b) _mm256_cmpeq_epi8(_mm256_max_epu8(a, b), a)
#define _mm256_cmple_epu8(a, b) _mm256_cmpge_epu8(b, a)
#define _mm256_cmpgt_epu8(a, b) _mm256_xor_si256(_mm256_cmple_epu8(a, b), _mm256_set1_epi8(-1))
#define _mm256_cmplt_epu8(a, b) _mm256_cmpgt_epu8(b, a)

#define simd_vec_type                __m256i
#define simd_vec_from_byte           _mm256_set1_epi8
#define simd_vec_load_from_mem(x)    _mm256_loadu_si256((__m256i const*) x)
#define simd_vec_to_memory(mem, vec) _mm256_storeu_si256((__m256i *) mem, (__m256i) vec)
#define simd_vec_eq                  _mm256_cmpeq_epi8
#define simd_vec_lt(a,b)             _mm256_cmplt_epu8(a, b)
#define simd_vec_gt(a,b)             _mm256_cmpgt_epu8(a, b)
#define simd_vec_or                  _mm256_or_si256
#define simd_vec_and                 _mm256_and_si256
#define simd_vec_max                 _mm256_max_epu8

void print_simd_vec(simd_vec_type vec) {
    alignas(32) unsigned char bytes[32];
    _mm256_storeu_si256((__m256i *) bytes, vec);
    printf("SIMD vector:\n\t[");
    for(int i=0; i< 32; i++) {
        printf(" %02x ", bytes[i]);
    }
    printf("]\n");
}

void print_simd_vec1(const char *prefix, simd_vec_type vec) {
    alignas(32) unsigned char bytes[32];
    _mm256_storeu_si256((__m256i *) bytes, vec);
    printf("%s:\n\t[", prefix);
    for(int i=0; i< 32; i++) {
        printf(" %02x ", bytes[i]);
    }
    printf("]\n");
}

int simd_vec_any_set(__m256i vec) {
    // print_simd_vec1("simd_vec_any_set vec", vec);
    __m256i zero = _mm256_setzero_si256();
    __m256i cmp  = _mm256_cmpeq_epi8(vec, zero);
    int mask     = _mm256_movemask_epi8(cmp);
    return mask != 0xFFFF;
}

int simd_vec_all_zero(__m256i vec) {
    // print_simd_vec1("simd_vec_any_set vec", vec);
    __m256i zero = _mm256_setzero_si256();
    __m256i cmp  = _mm256_cmpeq_epi8(vec, zero);
    int mask     = _mm256_movemask_epi8(cmp);
    return mask == 0xFFFF;
}

#elif HAVE_TYPE___M128I
#define SIMD_VEC_STRIDE          16

#define _mm_cmpge_epu8(a, b) _mm_cmpeq_epi8(_mm_max_epu8(a, b), a)
#define _mm_cmple_epu8(a, b) _mm_cmpge_epu8(b, a)
#define _mm_cmpgt_epu8(a, b) _mm_xor_si128(_mm_cmple_epu8(a, b), _mm_set1_epi8(-1))
#define _mm_cmplt_epu8(a, b) _mm_cmpgt_epu8(b, a)

#define simd_vec_type               __m128i
#define simd_vec_from_byte          _mm_set1_epi8
#define simd_vec_load_from_mem(x)   _mm_lddqu_si128((__m128i const*) x)
#define simd_vec_to_memory(mem, vec) _mm_storeu_si128((__m128i *) mem, (__m128i) vec)
#define simd_vec_eq                 _mm_cmpeq_epi8
#define simd_vec_lt(a,b)            _mm_cmplt_epu8(a, b)
#define simd_vec_gt(a,b)            _mm_cmpgt_epu8(a, b)
#define simd_vec_or                 _mm_or_si128
#define simd_vec_and                _mm_and_si128
#define simd_vec_max                _mm_max_epi8



void print_simd_vec(simd_vec_type vec) {
    alignas(16) unsigned char bytes[16];
    _mm_store_si128((__m128i *) bytes, vec);
    printf("SIMD vector:\n\t[");
    for(int i=0; i< 16; i++) {
        printf(" %02x ", bytes[i]);
    }
    printf("]\n");
}

void print_simd_vec1(const char *prefix, simd_vec_type vec) {
    alignas(16) unsigned char bytes[16];
    _mm_store_si128((__m128i *) bytes, vec);
    printf("%s:\n\t[", prefix);
    for(int i=0; i< 16; i++) {
        printf(" %02x ", bytes[i]);
    }
    printf("]\n");
}

int simd_vec_any_set(__m128i vec) {
    // print_simd_vec1("simd_vec_any_set vec", vec);
    __m128i zero = _mm_setzero_si128();
    __m128i cmp  = _mm_cmpeq_epi8(vec, zero);
    int mask     = _mm_movemask_epi8(cmp);
    return mask != 0xFFFF;
}

int simd_vec_all_zero(__m128i vec) {
    __m128i zero = _mm_setzero_si128();
    __m128i cmp  = _mm_cmpeq_epi8(vec, zero);
    int mask     = _mm_movemask_epi8(cmp);
    return mask  == 0xFFFF;
}

#endif /* HAVE_TYPE___M256 */
#endif /* HAVE_X86INTRIN_H */
#endif /* ENABLE_SIMD */
