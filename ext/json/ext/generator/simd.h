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

#endif /* HAVE_TYPE_UINT8X16_T */
#endif /* HAVE_ARM_NEON_H */

#endif /* ENABLE_SIMD */