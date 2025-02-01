#include "extconf.h"

typedef enum {
    SIMD_NONE,
    SIMD_NEON,
} SIMD_Implementation;

#ifdef ENABLE_SIMD

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>

#define FIND_SIMD_IMPLEMENTATION_DEFINED 1
SIMD_Implementation find_simd_implementation() {
    return SIMD_NEON;
}

#define HAVE_SIMD_NEON 1

uint8x16x4_t load_uint8x16_4(const unsigned char *table, int offset) {
  uint8x16x4_t tab;
  for(int i=0; i<4; i++) {
    tab.val[i] = vld1q_u8(table+offset+(i*16));
  }
  return tab;
}

void print_uint8x16(char *msg, uint8x16_t vec) {
  printf("%s\n[ ", msg);
  uint8_t store[16] = {0};
  vst1q_u8(store, vec);
  for(int i=0; i<16; i++) {
    printf("%3d ", store[i]);
  }
  printf("]\n");
}

#endif /* ARM Neon Support.*/

/* Other SIMD implementation checks here. */

#endif /* ENABLE_SIMD */

#ifndef FIND_SIMD_IMPLEMENTATION_DEFINED
SIMD_Implementation find_simd_implementation(void) {
    return SIMD_NONE;
}
#endif