typedef enum {
    SIMD_NONE,
    SIMD_NEON,
    SIMD_SSE2
} SIMD_Implementation;

#ifdef JSON_ENABLE_SIMD

#ifdef __clang__
  #if __has_builtin(__builtin_ctzll)
    #define HAVE_BUILTIN_CTZLL 1
  #else
    #define HAVE_BUILTIN_CTZLL 0
  #endif
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
  #define HAVE_BUILTIN_CTZLL 1
#else
  #define HAVE_BUILTIN_CTZLL 0
#endif

static inline uint32_t trailing_zeros64(uint64_t input) {
#if HAVE_BUILTIN_CTZLL
  return __builtin_ctzll(input);
#else
  uint32_t trailing_zeros = 0;
  uint64_t temp = input;
  while ((temp & 1) == 0 && temp > 0) {
    trailing_zeros++;
    temp >>= 1;
  }
  return trailing_zeros;
#endif
}

static inline int trailing_zeros(int input) {
  #if HAVE_BUILTIN_CTZLL
    return __builtin_ctz(input);
  #else
    int trailing_zeros = 0;
    int temp = input;
    while ((temp & 1) == 0 && temp > 0) {
      trailing_zeros++;
      temp >>= 1;
    }
    return trailing_zeros;
  #endif
}

#define SIMD_MINIMUM_THRESHOLD 6

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>

#define FIND_SIMD_IMPLEMENTATION_DEFINED 1
static SIMD_Implementation find_simd_implementation(void) {
    return SIMD_NEON;
}

#define HAVE_SIMD 1
#define HAVE_SIMD_NEON 1

#if (defined(__GNUC__ ) || defined(__clang__))
#define FORCE_INLINE __attribute__((always_inline))
#else
#define FORCE_INLINE
#endif

// See: https://community.arm.com/arm-community-blogs/b/servers-and-cloud-computing-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon
static inline FORCE_INLINE uint64_t neon_match_mask(uint8x16_t matches)
{
    const uint8x8_t res = vshrn_n_u16(vreinterpretq_u16_u8(matches), 4);
    const uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(res), 0);
    return mask & 0x8888888888888888ull;
}

static inline int FORCE_INLINE neon_vector_scan(void *state, int (*has_next_vector)(void *, size_t), const char *(*ptr)(void *),
                                                void (*advance_by)(void *, size_t), void (*set_match_mask)(void *, uint64_t))
{
    while (has_next_vector(state, sizeof(uint8x16_t))) {
        uint8x16_t chunk = vld1q_u8((const unsigned char *)ptr(state));

        // Trick: c < 32 || c == 34 can be factored as c ^ 2 < 33
        // https://lemire.me/blog/2025/04/13/detect-control-characters-quotes-and-backslashes-efficiently-using-swar/
        const uint8x16_t too_low_or_dbl_quote = vcltq_u8(veorq_u8(chunk, vdupq_n_u8(2)), vdupq_n_u8(33));

        uint8x16_t has_backslash = vceqq_u8(chunk, vdupq_n_u8('\\'));
        uint8x16_t needs_escape  = vorrq_u8(too_low_or_dbl_quote, has_backslash);
        uint64_t mask = neon_match_mask(needs_escape);
        if (mask) {
            set_match_mask(state, mask);
            return 1;
        }
        advance_by(state, sizeof(uint8x16_t));
    }

    return 0;
}

uint8x16x4_t load_uint8x16_4(const unsigned char *table) {
  uint8x16x4_t tab;
  tab.val[0] = vld1q_u8(table);
  tab.val[1] = vld1q_u8(table+16);
  tab.val[2] = vld1q_u8(table+32);
  tab.val[3] = vld1q_u8(table+48);
  return tab;
}

#endif /* ARM Neon Support.*/

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)

#ifdef HAVE_X86INTRIN_H
#include <x86intrin.h>

#define HAVE_SIMD 1
#define HAVE_SIMD_SSE2 1

#ifdef HAVE_CPUID_H
#define FIND_SIMD_IMPLEMENTATION_DEFINED 1

#include <cpuid.h>
#endif /* HAVE_CPUID_H */

static SIMD_Implementation find_simd_implementation(void) {

#if defined(__GNUC__ ) || defined(__clang__)
#ifdef __GNUC__ 
    __builtin_cpu_init();
#endif /* __GNUC__  */

    // TODO Revisit. I think the SSE version now only uses SSE2 instructions.
    if (__builtin_cpu_supports("sse2")) {
        return SIMD_SSE2;
    }
#endif /* __GNUC__ || __clang__*/

    return SIMD_NONE;
}

#endif /* HAVE_X86INTRIN_H */
#endif /* X86_64 Support */

#endif /* JSON_ENABLE_SIMD */

#ifndef FIND_SIMD_IMPLEMENTATION_DEFINED
static SIMD_Implementation find_simd_implementation(void) {
    return SIMD_NONE;
}
#endif
