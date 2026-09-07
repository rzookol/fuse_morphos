#ifndef FUSE_UI_MORPHOS_MORPHOSVIDEO_ALTIVEC_H
#define FUSE_UI_MORPHOS_MORPHOSVIDEO_ALTIVEC_H

#include <stddef.h>
#include <stdint.h>

/* AltiVec-only kernels.  This translation unit is compiled with
   -maltivec; callers must runtime-check CPU support first.
   No vector types cross the public ABI. */
void morphosvideo_swap_rgb565le_altivec( const uint16_t *src, uint8_t *dst,
                                         size_t pixels );
void morphosvideo_rgb565_to_argb8888_altivec( const uint16_t *src,
                                              uint32_t *dst, size_t pixels );
void morphosvideo_rgb565_to_rgba8888_altivec( const uint16_t *src,
                                              uint8_t *dst, size_t pixels );

#endif
