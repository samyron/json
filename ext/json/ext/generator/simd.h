#include "extconf.h"

typedef enum {
    SIMD_NONE,
    SIMD_NEON,
    SIMD_SSE42,
    SIMD_AVX2
} SIMD_Implementation;

#ifdef ENABLE_SIMD

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>

#define FIND_SIMD_IMPLEMENTATION_DEFINED 1
SIMD_Implementation find_simd_implementation() {
    return SIMD_NEON;
}

#define HAVE_SIMD_NEON 1

#ifdef HAVE_TYPE_UINT8X16_T

#endif /* HAVE_TYPE_UINT8X16_T */
#endif /* ARM Neon Support.*/

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)

#define HAVE_SIMD_X86_64 1 
#ifdef HAVE_X86INTRIN_H
#include <x86intrin.h>

#define HAVE_SIMD_X86_64 1

#ifdef HAVE_CPUID_H
#define FIND_SIMD_IMPLEMENTATION_DEFINED 1

#include <cpuid.h>
#endif 

SIMD_Implementation find_simd_implementation(void) {

#if defined(__GNUC__ ) || defined(__clang__)
#ifdef __GNUC__ 
    __builtin_cpu_init();
#endif /* __GNUC__  */

#ifdef HAVE_TYPE___M256I
    if(__builtin_cpu_supports("avx2")) {
        return SIMD_AVX2;
    }
#endif /* #ifdef HAVE_TYPE___M256I */

    // TODO Revisit. I think the SSE version now only uses SSE2 instructions.
    if (__builtin_cpu_supports("sse4.2")) {
        return SIMD_SSE42;
    }
#endif /* __GNUC__ || __clang__*/

    return SIMD_NONE;
}

#endif /* HAVE_X86INTRIN_H */
#endif /* X86_64 Support */
#endif /* ENABLE_SIMD */

#ifndef FIND_SIMD_IMPLEMENTATION_DEFINED
SIMD_Implementation find_simd_implementation(void) {
    return SIMD_NONE;
}
#endif