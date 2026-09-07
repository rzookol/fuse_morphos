/* morphosvideo_altivec.c: G4/G5 VMX kernels for the MorphOS presenter.

   This file is intentionally compiled on its own with -maltivec only.
   Exported entry points use only the normal scalar PPC ABI,
   so the rest of Fuse remains runnable on machines without AltiVec. */

#include <stddef.h>
#include <stdint.h>
#include <altivec.h>

#include "morphosvideo_altivec.h"

static inline vector unsigned char
load_unaligned_16( const void *ptr )
{
  const unsigned char *p = (const unsigned char*)ptr;
  vector unsigned char a = vec_ld( 0, p );
  vector unsigned char b = vec_ld( 15, p );
  vector unsigned char align = vec_lvsl( 0, p );
  return vec_perm( a, b, align );
}

void
morphosvideo_swap_rgb565le_altivec( const uint16_t *src16, uint8_t *dst,
                                    size_t pixels )
{
  const uint8_t *src = (const uint8_t*)src16;
  const vector unsigned char swap16 = (vector unsigned char){
    1,0, 3,2, 5,4, 7,6, 9,8, 11,10, 13,12, 15,14
  };

  /* Classic AltiVec vec_st is naturally used on a 16-byte boundary.  Peel
     only the destination; unaligned source is assembled with lvsl/perm. */
  while( pixels && ( (uintptr_t)dst & 15u ) ) {
    dst[0] = src[1]; dst[1] = src[0];
    src += 2; dst += 2; pixels--;
  }

  /* An unaligned vec_ld pair can read up to 15 bytes beyond the requested
     16-byte window. Leave the final vector-sized block to the scalar tail
     unless the source itself is aligned, so the last scanline never reads
     past the framebuffer allocation. */
  while( pixels >= 16u ) {
    vector unsigned char in = load_unaligned_16( src );
    vector unsigned char out = vec_perm( in, in, swap16 );
    vec_st( out, 0, dst );
    src += 16; dst += 16; pixels -= 8u;
  }
  if( pixels >= 8u && !( (uintptr_t)src & 15u ) ) {
    vector unsigned char in = vec_ld( 0, src );
    vector unsigned char out = vec_perm( in, in, swap16 );
    vec_st( out, 0, dst );
    src += 16; dst += 16; pixels -= 8u;
  }

  while( pixels-- ) {
    dst[0] = src[1]; dst[1] = src[0];
    src += 2; dst += 2;
  }
}

static inline void
rgb565_channels_8( vector unsigned short p,
                    vector unsigned char *r8,
                    vector unsigned char *g8,
                    vector unsigned char *b8 )
{
  const vector unsigned short sh11 = vec_splats( (unsigned short)11 );
  const vector unsigned short sh5  = vec_splats( (unsigned short)5 );
  const vector unsigned short sh4  = vec_splats( (unsigned short)4 );
  const vector unsigned short sh3  = vec_splats( (unsigned short)3 );
  const vector unsigned short sh2  = vec_splats( (unsigned short)2 );
  const vector unsigned short mask6 = vec_splats( (unsigned short)0x003f );
  const vector unsigned short mask5 = vec_splats( (unsigned short)0x001f );
  vector unsigned short r = vec_sr( p, sh11 );
  vector unsigned short g = vec_and( vec_sr( p, sh5 ), mask6 );
  vector unsigned short b = vec_and( p, mask5 );

  r = vec_or( vec_sl( r, sh3 ), vec_sr( r, sh2 ) );
  g = vec_or( vec_sl( g, sh2 ), vec_sr( g, sh4 ) );
  b = vec_or( vec_sl( b, sh3 ), vec_sr( b, sh2 ) );

  /* vec_pack consumes two vectors; duplicating the input gives the eight
     wanted bytes in the high half, which mergeh() below consumes. */
  *r8 = vec_pack( r, r );
  *g8 = vec_pack( g, g );
  *b8 = vec_pack( b, b );
}

void
morphosvideo_rgb565_to_argb8888_altivec( const uint16_t *src,
                                         uint32_t *dst, size_t pixels )
{
  const vector unsigned char alpha = vec_splats( (unsigned char)0xff );

  while( pixels && ( (uintptr_t)dst & 15u ) ) {
    uint16_t c = *src++;
    unsigned r = ( c >> 11 ) & 31u;
    unsigned g = ( c >> 5 ) & 63u;
    unsigned b = c & 31u;
    r = ( r << 3 ) | ( r >> 2 );
    g = ( g << 2 ) | ( g >> 4 );
    b = ( b << 3 ) | ( b >> 2 );
    *dst++ = 0xff000000u | ( r << 16 ) | ( g << 8 ) | b;
    pixels--;
  }

  while( pixels >= 16u ) {
    vector unsigned short p = (vector unsigned short)load_unaligned_16( src );
    vector unsigned char r8, g8, b8;
    vector unsigned char ar, gb, out0, out1;
    rgb565_channels_8( p, &r8, &g8, &b8 );
    ar = vec_mergeh( alpha, r8 );
    gb = vec_mergeh( g8, b8 );
    out0 = (vector unsigned char)vec_mergeh( (vector unsigned short)ar,
                                             (vector unsigned short)gb );
    out1 = (vector unsigned char)vec_mergel( (vector unsigned short)ar,
                                             (vector unsigned short)gb );
    vec_st( out0, 0, (unsigned char*)dst );
    vec_st( out1, 16, (unsigned char*)dst );
    src += 8; dst += 8; pixels -= 8u;
  }
  if( pixels >= 8u && !( (uintptr_t)src & 15u ) ) {
    vector unsigned short p = (vector unsigned short)vec_ld( 0, src );
    vector unsigned char r8, g8, b8;
    vector unsigned char ar, gb, out0, out1;
    rgb565_channels_8( p, &r8, &g8, &b8 );
    ar = vec_mergeh( alpha, r8 );
    gb = vec_mergeh( g8, b8 );
    out0 = (vector unsigned char)vec_mergeh( (vector unsigned short)ar,
                                             (vector unsigned short)gb );
    out1 = (vector unsigned char)vec_mergel( (vector unsigned short)ar,
                                             (vector unsigned short)gb );
    vec_st( out0, 0, (unsigned char*)dst );
    vec_st( out1, 16, (unsigned char*)dst );
    src += 8; dst += 8; pixels -= 8u;
  }

  while( pixels-- ) {
    uint16_t c = *src++;
    unsigned r = ( c >> 11 ) & 31u;
    unsigned g = ( c >> 5 ) & 63u;
    unsigned b = c & 31u;
    r = ( r << 3 ) | ( r >> 2 );
    g = ( g << 2 ) | ( g >> 4 );
    b = ( b << 3 ) | ( b >> 2 );
    *dst++ = 0xff000000u | ( r << 16 ) | ( g << 8 ) | b;
  }
}

void
morphosvideo_rgb565_to_rgba8888_altivec( const uint16_t *src,
                                         uint8_t *dst, size_t pixels )
{
  const vector unsigned char alpha = vec_splats( (unsigned char)0xff );

  while( pixels && ( (uintptr_t)dst & 15u ) ) {
    uint16_t c = *src++;
    unsigned r = ( c >> 11 ) & 31u;
    unsigned g = ( c >> 5 ) & 63u;
    unsigned b = c & 31u;
    dst[0] = (uint8_t)( ( r << 3 ) | ( r >> 2 ) );
    dst[1] = (uint8_t)( ( g << 2 ) | ( g >> 4 ) );
    dst[2] = (uint8_t)( ( b << 3 ) | ( b >> 2 ) );
    dst[3] = 0xff;
    dst += 4; pixels--;
  }

  while( pixels >= 16u ) {
    vector unsigned short p = (vector unsigned short)load_unaligned_16( src );
    vector unsigned char r8, g8, b8;
    vector unsigned char rg, ba, out0, out1;
    rgb565_channels_8( p, &r8, &g8, &b8 );
    rg = vec_mergeh( r8, g8 );
    ba = vec_mergeh( b8, alpha );
    out0 = (vector unsigned char)vec_mergeh( (vector unsigned short)rg,
                                             (vector unsigned short)ba );
    out1 = (vector unsigned char)vec_mergel( (vector unsigned short)rg,
                                             (vector unsigned short)ba );
    vec_st( out0, 0, dst );
    vec_st( out1, 16, dst );
    src += 8; dst += 32; pixels -= 8u;
  }
  if( pixels >= 8u && !( (uintptr_t)src & 15u ) ) {
    vector unsigned short p = (vector unsigned short)vec_ld( 0, src );
    vector unsigned char r8, g8, b8;
    vector unsigned char rg, ba, out0, out1;
    rgb565_channels_8( p, &r8, &g8, &b8 );
    rg = vec_mergeh( r8, g8 );
    ba = vec_mergeh( b8, alpha );
    out0 = (vector unsigned char)vec_mergeh( (vector unsigned short)rg,
                                             (vector unsigned short)ba );
    out1 = (vector unsigned char)vec_mergel( (vector unsigned short)rg,
                                             (vector unsigned short)ba );
    vec_st( out0, 0, dst );
    vec_st( out1, 16, dst );
    src += 8; dst += 32; pixels -= 8u;
  }

  while( pixels-- ) {
    uint16_t c = *src++;
    unsigned r = ( c >> 11 ) & 31u;
    unsigned g = ( c >> 5 ) & 63u;
    unsigned b = c & 31u;
    dst[0] = (uint8_t)( ( r << 3 ) | ( r >> 2 ) );
    dst[1] = (uint8_t)( ( g << 2 ) | ( g >> 4 ) );
    dst[2] = (uint8_t)( ( b << 3 ) | ( b >> 2 ) );
    dst[3] = 0xff;
    dst += 4;
  }
}
