/* morphosvideo.c: MorphOS native Surface / CGXVideo Overlay / TinyGL presenter.
   Accelerated renderers are optional and fall back TinyGL -> Overlay -> Surface.
   Overlay and TinyGL receive the native RGB565 framebuffer and scale in hardware. */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>

#include "morphosvideo.h"
#include "morphosvideo_altivec.h"
#include "morphosdebug.h"

#ifdef __MORPHOS__
#include <exec/libraries.h>
#include <exec/system.h>
#include <cybergraphx/cgxvideo.h>
#include <cybergraphx/cybergraphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <proto/cgxvideo.h>
#include <proto/cybergraphics.h>
#include <proto/exec.h>
#ifdef _NO_PPCINLINE
#include <ppcinline/exec.h>
#endif
#include <proto/graphics.h>
#include <proto/tinygl.h>
#include <tgl/gl.h>
#include <tgl/gla.h>

#ifndef BMF_3DTARGET
#define BMF_3DTARGET (1UL << 12)
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

struct Library *CGXVideoBase = NULL;
struct Library *TinyGLBase = NULL;
#endif

typedef struct morphos_video_state {
  unsigned source_width, source_height, source_pitch;
  unsigned width, height;
  int left, top;
  void *window;
  morphos_video_mode requested, active;
  uint32_t *argb;
  size_t argb_capacity;
  uint32_t *scaled_argb;
  size_t scaled_capacity;
  uint8_t *rgba;
  size_t rgba_capacity;
  void *vlayer;
  int overlay_bars_dirty;
  int overlay_has_frame;
  int overlay_geom_valid;
  int overlay_left_indent, overlay_right_indent;
  int overlay_top_indent, overlay_bottom_indent;
  void *gl_context;
  void *gl_bitmap;
  uint32_t gl_texture;
  int gl_texture_ready;
  int vsync, linear_filter;
  int fullscreen;
} morphos_video_state;

static morphos_video_state v = {
  320, 240, 640, 1, 1, 0, 0, NULL,
  MORPHOS_VIDEO_SURFACE, MORPHOS_VIDEO_SURFACE,
  NULL, 0, NULL, 0, NULL, 0,
  NULL, 1, 0, 0, 0, 0, 0, 0, NULL, NULL, 0, 0, 0, 1, 0
};
static unsigned video_trace_present_count;
static int morphos_has_altivec;
static uint32_t rgb565_hi_lut[256];
static uint32_t rgb565_lo_lut[256];
static int rgb565_lut_ready;


static int attach_surface( void );
static int attach_overlay( void );
static int attach_tinygl( void );
static int present_surface( const libspectrum_word *pixels, unsigned pitch );
static int present_overlay( const libspectrum_word *pixels, unsigned pitch );
static int present_tinygl( const libspectrum_word *pixels, unsigned pitch );
static void destroy_overlay( void );
static void destroy_tinygl( void );
static void update_overlay_geometry( void );
static void clear_overlay_bars( void );

#ifdef __MORPHOS__
static struct Window *
video_window( void )
{
  /* Same ownership model as openMSX MorphOS: v.window is supplied by MUI's
     MUIM_Show path and remains valid until detach/MUIM_Hide.  Validate only
     the fields required by the individual presenter path. */
  return (struct Window*)v.window;
}
#endif

static int
ensure_buffer( void **buffer, size_t *capacity, size_t bytes )
{
  void *p;
  if( bytes <= *capacity ) return 1;
  p = realloc( *buffer, bytes );
  if( !p ) return 0;
  *buffer = p;
  *capacity = bytes;
  return 1;
}

static void
init_rgb565_lut( void )
{
  unsigned h, l;
  if( rgb565_lut_ready ) return;
  for( h = 0; h < 256; h++ ) {
    unsigned r5 = h >> 3;
    unsigned gh = h & 7;
    unsigned r8 = ( r5 << 3 ) | ( r5 >> 2 );
    unsigned g_hi = ( gh << 5 ) | ( gh >> 1 );
    rgb565_hi_lut[h] = ( r8 << 16 ) | ( g_hi << 8 );
  }
  for( l = 0; l < 256; l++ ) {
    unsigned gl = l >> 5;
    unsigned b5 = l & 31;
    unsigned g_lo = gl << 2;
    unsigned b8 = ( b5 << 3 ) | ( b5 >> 2 );
    rgb565_lo_lut[l] = ( g_lo << 8 ) | b8;
  }
  rgb565_lut_ready = 1;
}

static inline uint32_t
rgb565_to_argb_lut( uint16_t c )
{
  return 0xff000000UL | rgb565_hi_lut[c >> 8] | rgb565_lo_lut[c & 0xff];
}

static void
convert_argb_scalar( const libspectrum_word *pixels, unsigned pitch )
{
  unsigned x, y;
  for( y = 0; y < v.source_height; y++ ) {
    const libspectrum_word *src = (const libspectrum_word*)
      ( (const uint8_t*)pixels + (size_t)y * pitch );
    uint32_t *dst = v.argb + (size_t)y * v.source_width;
    for( x = 0; x < v.source_width; x++ )
      dst[x] = rgb565_to_argb_lut( src[x] );
  }
}

static void
convert_argb( const libspectrum_word *pixels, unsigned pitch )
{
  unsigned y;
  if( !morphos_has_altivec || v.source_width < 8 ) {
    convert_argb_scalar( pixels, pitch );
    return;
  }
  for( y = 0; y < v.source_height; y++ ) {
    const libspectrum_word *src = (const libspectrum_word*)
      ( (const uint8_t*)pixels + (size_t)y * pitch );
    uint32_t *dst = v.argb + (size_t)y * v.source_width;
    morphosvideo_rgb565_to_argb8888_altivec( src, dst, v.source_width );
  }
}

static void
convert_rgba_scalar( const libspectrum_word *pixels, unsigned pitch )
{
  unsigned x, y;
  for( y = 0; y < v.source_height; y++ ) {
    const libspectrum_word *src = (const libspectrum_word*)
      ( (const uint8_t*)pixels + (size_t)y * pitch );
    uint32_t *dst = (uint32_t*)( v.rgba + (size_t)y * v.source_width * 4 );
    for( x = 0; x < v.source_width; x++ ) {
      uint32_t argb = rgb565_to_argb_lut( src[x] );
      dst[x] = ( argb << 8 ) | ( argb >> 24 );
    }
  }
}

static void
convert_rgba( const libspectrum_word *pixels, unsigned pitch )
{
  unsigned y;
  if( !morphos_has_altivec || v.source_width < 8 ) {
    convert_rgba_scalar( pixels, pitch );
    return;
  }
  for( y = 0; y < v.source_height; y++ ) {
    const libspectrum_word *src = (const libspectrum_word*)
      ( (const uint8_t*)pixels + (size_t)y * pitch );
    uint8_t *dst = v.rgba + (size_t)y * v.source_width * 4;
    morphosvideo_rgb565_to_rgba8888_altivec( src, dst, v.source_width );
  }
}

int
morphosvideo_init( void )
{
#ifdef __MORPHOS__
#ifdef SYSTEMINFOTYPE_PPC_ALTIVEC
  {
    uint32_t available = 0;
    ULONG copied = NewGetSystemAttrsA( &available, sizeof( available ),
                                       SYSTEMINFOTYPE_PPC_ALTIVEC,
                                       (struct TagItem*)0 );
    morphos_has_altivec = copied != 0 && available != 0;
  }
#else
  morphos_has_altivec = 0;
#endif
  init_rgb565_lut();
  if( !CGXVideoBase ) CGXVideoBase = OpenLibrary( "cgxvideo.library", 0 );
  if( !TinyGLBase ) TinyGLBase = OpenLibrary( "tinygl.library", 0 );
  MOSDBG( "video_init SURFACE+OVERLAY+TINYGL cgxvideo=%08lx tinygl=%08lx altivec=%ld",
          MOSPTR( CGXVideoBase ), MOSPTR( TinyGLBase ), (LONG)morphos_has_altivec );
#else
  morphos_has_altivec = 0;
  init_rgb565_lut();
  MOSDBG( "video_init SURFACE+OVERLAY+TINYGL non-MorphOS" );
#endif
  return 1;
}

void
morphosvideo_end( void )
{
  MOSDBG( "video_end begin active=%s window=%08lx layer=%08lx gl=%08lx",
          morphosvideo_active_mode_name(), MOSPTR( v.window ), MOSPTR( v.vlayer ),
          MOSPTR( v.gl_context ) );
  morphosvideo_detach();
  free( v.argb ); v.argb = NULL; v.argb_capacity = 0;
  free( v.scaled_argb ); v.scaled_argb = NULL; v.scaled_capacity = 0;
  free( v.rgba ); v.rgba = NULL; v.rgba_capacity = 0;
#ifdef __MORPHOS__
  if( TinyGLBase ) CloseLibrary( TinyGLBase );
  TinyGLBase = NULL;
  if( CGXVideoBase ) CloseLibrary( CGXVideoBase );
  CGXVideoBase = NULL;
#endif
  MOSDBG( "video_end done" );
}

int
morphosvideo_has_overlay( void )
{
#ifdef __MORPHOS__
  return CGXVideoBase != NULL;
#else
  return 0;
#endif
}

int
morphosvideo_has_tinygl( void )
{
#ifdef __MORPHOS__
  return TinyGLBase != NULL;
#else
  return 0;
#endif
}

int
morphosvideo_configure( unsigned sw, unsigned sh, unsigned pitch )
{
  size_t bytes;
  int geometry_changed;
  if( !sw || !sh || pitch < sw * 2 ) return 0;

  geometry_changed = sw != v.source_width || sh != v.source_height;
  v.source_width = sw;
  v.source_height = sh;
  v.source_pitch = pitch;
  bytes = (size_t)sw * sh * sizeof( *v.argb );
  if( !ensure_buffer( (void**)&v.argb, &v.argb_capacity, bytes ) ) return 0;
  if( !ensure_buffer( (void**)&v.rgba, &v.rgba_capacity, (size_t)sw * sh * 4 ) ) return 0;
  if( geometry_changed ) v.gl_texture_ready = 0;

  /* This function runs for every emulated frame. KPrintF here was a major
     diagnostic-only performance hit, especially while MUI was resizing.
     Log only actual source-geometry changes. */
  if( geometry_changed )
    MOSDBG( "video_configure src=%lux%lu pitch=%lu active=%s requested=%ld changed=1",
            (ULONG)sw, (ULONG)sh, (ULONG)pitch,
            morphosvideo_active_mode_name(), (LONG)v.requested );

  if( geometry_changed && v.window ) {
    if( v.active == MORPHOS_VIDEO_OVERLAY ) {
      destroy_overlay();
      if( attach_overlay() ) {
        v.active = MORPHOS_VIDEO_OVERLAY;
        MOSDBG( "video_configure overlay recreated" );
        return 1;
      }
      MOSDBG( "video_configure overlay recreate failed -> Surface" );
      v.active = MORPHOS_VIDEO_SURFACE;
      return attach_surface();
    }
    /* TinyGL texture storage is recreated lazily by present_tinygl(). */
    if( v.active == MORPHOS_VIDEO_TINYGL ) return 1;

    if( v.requested == MORPHOS_VIDEO_TINYGL ) {
      if( attach_tinygl() ) {
        v.active = MORPHOS_VIDEO_TINYGL;
        MOSDBG( "video_configure Surface -> TinyGL" );
        return 1;
      }
      if( attach_overlay() ) {
        v.active = MORPHOS_VIDEO_OVERLAY;
        MOSDBG( "video_configure TinyGL unavailable -> Overlay" );
        return 1;
      }
      MOSDBG( "video_configure requested TinyGL unavailable -> Surface" );
    } else if( v.requested == MORPHOS_VIDEO_OVERLAY ) {
      if( attach_overlay() ) {
        v.active = MORPHOS_VIDEO_OVERLAY;
        MOSDBG( "video_configure Surface -> Overlay" );
        return 1;
      }
      MOSDBG( "video_configure requested Overlay unavailable -> Surface" );
    }
  }
  return 1;
}

int
morphosvideo_attach( void *window, int left, int top,
                     unsigned width, unsigned height )
{
  morphosvideo_detach();
  v.window = window;
  v.left = left;
  v.top = top;
  v.width = width ? width : 1;
  v.height = height ? height : 1;

  MOSDBG( "video_attach window=%08lx geom=%ld,%ld %lux%lu requested=%ld",
          MOSPTR( window ), (LONG)left, (LONG)top,
          (ULONG)v.width, (ULONG)v.height, (LONG)v.requested );

  if( !window ) return 0;
  if( v.requested == MORPHOS_VIDEO_TINYGL ) {
    if( attach_tinygl() ) {
      v.active = MORPHOS_VIDEO_TINYGL;
      MOSDBG( "video_attach active=TinyGL" );
      return 1;
    }
    MOSDBG( "video_attach TinyGL failed -> Overlay fallback" );
    if( attach_overlay() ) {
      v.active = MORPHOS_VIDEO_OVERLAY;
      MOSDBG( "video_attach active=Overlay (TinyGL fallback)" );
      return 1;
    }
    MOSDBG( "video_attach Overlay fallback failed -> Surface" );
  } else if( v.requested == MORPHOS_VIDEO_OVERLAY ) {
    if( attach_overlay() ) {
      v.active = MORPHOS_VIDEO_OVERLAY;
      MOSDBG( "video_attach active=Overlay" );
      return 1;
    }
    MOSDBG( "video_attach Overlay failed -> Surface" );
  }

  v.active = MORPHOS_VIDEO_SURFACE;
  if( attach_surface() ) {
    MOSDBG( "video_attach active=Surface" );
    return 1;
  }
  MOSDBG( "video_attach Surface failed" );
  return 0;
}

void
morphosvideo_detach( void )
{
  if( v.gl_context || v.vlayer || v.window )
    MOSDBG( "video_detach active=%s window=%08lx layer=%08lx gl=%08lx",
            morphosvideo_active_mode_name(), MOSPTR( v.window ), MOSPTR( v.vlayer ),
            MOSPTR( v.gl_context ) );
  destroy_tinygl();
  destroy_overlay();
  v.window = NULL;
  v.active = MORPHOS_VIDEO_SURFACE;
}

int
morphosvideo_set_mode( morphos_video_mode mode )
{
  void *window;
  int left, top;
  unsigned width, height;

  if( mode != MORPHOS_VIDEO_SURFACE && mode != MORPHOS_VIDEO_OVERLAY &&
      mode != MORPHOS_VIDEO_TINYGL ) return 0;

  MOSDBG( "video_set_mode requested=%ld current=%s has_overlay=%ld has_tinygl=%ld",
          (LONG)mode, morphosvideo_active_mode_name(),
          (LONG)morphosvideo_has_overlay(), (LONG)morphosvideo_has_tinygl() );
  v.requested = mode;
  if( !v.window ) return 1;

  window = v.window;
  left = v.left; top = v.top; width = v.width; height = v.height;
  return morphosvideo_attach( window, left, top, width, height );
}

morphos_video_mode
morphosvideo_requested_mode( void )
{
  return v.requested;
}

morphos_video_mode
morphosvideo_active_mode( void )
{
  return v.active;
}

const char *
morphosvideo_active_mode_name( void )
{
  return v.active == MORPHOS_VIDEO_TINYGL ? "TinyGL" :
         v.active == MORPHOS_VIDEO_OVERLAY ? "Overlay" : "Surface";
}

void
morphosvideo_set_vsync( int enabled )
{
  v.vsync = enabled ? 1 : 0;
#ifdef __MORPHOS__
  if( v.active == MORPHOS_VIDEO_TINYGL && v.gl_context )
    GLASetSync( (GLContext*)v.gl_context, v.vsync ? 1 : 0 );
#endif
}

void
morphosvideo_set_fullscreen( int enabled )
{
  enabled = enabled ? 1 : 0;
  if( v.fullscreen == enabled ) return;
  v.fullscreen = enabled;
  v.overlay_geom_valid = 0;
  v.overlay_bars_dirty = 1;
#ifdef __MORPHOS__
  if( v.active == MORPHOS_VIDEO_OVERLAY && v.vlayer )
    update_overlay_geometry();
#endif
}

void
morphosvideo_set_linear_filter( int enabled )
{
  int new_value = enabled ? 1 : 0;
  if( v.linear_filter == new_value ) return;
  v.linear_filter = new_value;
  MOSDBG( "video linear_filter=%ld", (LONG)v.linear_filter );

#ifdef __MORPHOS__
  if( v.active == MORPHOS_VIDEO_TINYGL && v.gl_context && v.gl_texture ) {
    GLContext *context = (GLContext*)v.gl_context;
    GLBindTexture( context, GL_TEXTURE_2D, v.gl_texture );
    GLTexParameteri( context, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                     v.linear_filter ? GL_LINEAR : GL_NEAREST );
    GLTexParameteri( context, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                     v.linear_filter ? GL_LINEAR : GL_NEAREST );
  }
#endif

  /* CGXVideo's filter flag is selected when the layer is created. */
  if( v.window && v.active == MORPHOS_VIDEO_OVERLAY ) {
    void *window = v.window;
    int left = v.left, top = v.top;
    unsigned width = v.width, height = v.height;
    (void)morphosvideo_attach( window, left, top, width, height );
  }
}

int morphosvideo_vsync( void ) { return v.vsync; }
int morphosvideo_linear_filter( void ) { return v.linear_filter; }

void
morphosvideo_content_rect( unsigned *x, unsigned *y,
                           unsigned *w, unsigned *h )
{
  unsigned rx = 0, ry = 0, rw = v.width, rh = v.height;
  unsigned aspect_w, aspect_h;
  int overlay_rect;

  if( !v.source_width || !v.source_height ) goto done;

  /* The fullscreen MUI window itself fills the complete private Screen, but
     accelerated video must still preserve its display aspect ratio inside
     that Area. This intentionally leaves black pillarbox/letterbox bars when
     the screen aspect differs from the emulated picture. Surface remains
     pixel-sized/fixed by MUI. CGXVideo uses the Spectrum's intended 4:3
     display aspect; TinyGL uses the native source geometry. */
  overlay_rect = v.active == MORPHOS_VIDEO_OVERLAY || v.vlayer != NULL;

  if( v.active == MORPHOS_VIDEO_SURFACE && !overlay_rect ) goto done;
  aspect_w = overlay_rect ? 4u : v.source_width;
  aspect_h = overlay_rect ? 3u : v.source_height;

  if( (uint64_t)v.width * aspect_h > (uint64_t)v.height * aspect_w ) {
    rw = (unsigned)( (uint64_t)v.height * aspect_w / aspect_h );
    if( !rw ) rw = 1;
    rx = ( v.width - rw ) / 2;
  } else if( (uint64_t)v.width * aspect_h < (uint64_t)v.height * aspect_w ) {
    rh = (unsigned)( (uint64_t)v.width * aspect_h / aspect_w );
    if( !rh ) rh = 1;
    ry = ( v.height - rh ) / 2;
  }

done:
  if( x ) *x = rx;
  if( y ) *y = ry;
  if( w ) *w = rw;
  if( h ) *h = rh;
}

void
morphosvideo_set_geometry( int left, int top, unsigned width, unsigned height )
{
  unsigned new_width = width ? width : 1;
  unsigned new_height = height ? height : 1;
  int size_changed = new_width != v.width || new_height != v.height;

  v.left = left;
  v.top = top;
  v.width = new_width;
  v.height = new_height;
  if( v.active == MORPHOS_VIDEO_OVERLAY ) {
    update_overlay_geometry();
    return;
  }
  if( v.active == MORPHOS_VIDEO_TINYGL && size_changed ) {
    destroy_tinygl();
    if( attach_tinygl() ) {
      v.active = MORPHOS_VIDEO_TINYGL;
      return;
    }
    MOSDBG( "TinyGL resize recreate failed -> Overlay fallback" );
    if( attach_overlay() ) {
      v.active = MORPHOS_VIDEO_OVERLAY;
      return;
    }
    v.active = MORPHOS_VIDEO_SURFACE;
  }
}

static int
attach_surface( void )
{
#ifdef __MORPHOS__
  {
    struct Window *w = video_window();
    return w && w->RPort;
  }
#else
  return 0;
#endif
}

static int
present_surface( const libspectrum_word *pixels, unsigned pitch )
{
#ifdef __MORPHOS__
  struct Window *w = video_window();
  unsigned x, y;
  if( !pixels || !w || !w->RPort ) return 0;
  convert_argb( pixels, pitch );

  if( v.width == v.source_width && v.height == v.source_height ) {
    WritePixelArray( v.argb, 0, 0, v.source_width * 4, w->RPort,
                     v.left, v.top, v.source_width, v.source_height,
                     RECTFMT_ARGB );
  } else {
    size_t count = (size_t)v.width * v.height;
    if( !ensure_buffer( (void**)&v.scaled_argb, &v.scaled_capacity,
                        count * sizeof( *v.scaled_argb ) ) ) return 0;
    for( y = 0; y < v.height; y++ ) {
      unsigned sy = (unsigned)( (uint64_t)y * v.source_height / v.height );
      uint32_t *dst = v.scaled_argb + (size_t)y * v.width;
      const uint32_t *src = v.argb + (size_t)sy * v.source_width;
      for( x = 0; x < v.width; x++ )
        dst[x] = src[(unsigned)( (uint64_t)x * v.source_width / v.width )];
    }
    WritePixelArray( v.scaled_argb, 0, 0, v.width * 4, w->RPort,
                     v.left, v.top, v.width, v.height, RECTFMT_ARGB );
  }
  return 1;
#else
  (void)pixels; (void)pitch;
  return 0;
#endif
}

static int
attach_tinygl( void )
{
#ifdef __MORPHOS__
  struct Window *w;
  struct BitMap *friend_bitmap, *bitmap;
  GLContext *context;
  ULONG depth;
  struct TagItem tags[3];

  if( !TinyGLBase ) {
    MOSDBG( "TinyGL attach: tinygl.library unavailable" );
    return 0;
  }
  w = video_window();
  if( !w || !w->RPort || !w->RPort->BitMap ) {
    MOSDBG( "TinyGL attach: invalid window/rport/bitmap win=%08lx rport=%08lx",
            MOSPTR( w ), MOSPTR( w ? w->RPort : NULL ) );
    return 0;
  }
  friend_bitmap = w->RPort->BitMap;
  depth = GetBitMapAttr( friend_bitmap, BMA_DEPTH );
  if( depth < 15 ) {
    MOSDBG( "TinyGL attach: screen depth %lu unsupported", depth );
    return 0;
  }

  context = GLInit();
  if( !context ) {
    MOSDBG( "TinyGL GLInit FAILED" );
    return 0;
  }
  bitmap = AllocBitMap( v.width, v.height, depth,
      BMF_CLEAR | BMF_MINPLANES | BMF_DISPLAYABLE | BMF_3DTARGET, friend_bitmap );
  if( !bitmap ) {
    MOSDBG( "TinyGL AllocBitMap FAILED %lux%lu depth=%lu",
            (ULONG)v.width, (ULONG)v.height, depth );
    GLClose( context );
    return 0;
  }

  tags[0].ti_Tag = TGL_CONTEXT_BITMAP; tags[0].ti_Data = (ULONG)bitmap;
  tags[1].ti_Tag = TGL_CONTEXT_STENCIL; tags[1].ti_Data = TRUE;
  tags[2].ti_Tag = TAG_DONE; tags[2].ti_Data = 0;
  if( GLAInitializeContext( context, tags ) <= 0 ) {
    MOSDBG( "TinyGL GLAInitializeContext FAILED" );
    GLClose( context );
    FreeBitMap( bitmap );
    return 0;
  }

  v.gl_context = context;
  v.gl_bitmap = bitmap;
  v.gl_texture = 0;
  v.gl_texture_ready = 0;
  GLASetSync( context, v.vsync ? 1 : 0 );
  GLGenTextures( context, 1, &v.gl_texture );
  GLBindTexture( context, GL_TEXTURE_2D, v.gl_texture );
  GLTexParameteri( context, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                   v.linear_filter ? GL_LINEAR : GL_NEAREST );
  GLTexParameteri( context, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                   v.linear_filter ? GL_LINEAR : GL_NEAREST );
  GLTexParameteri( context, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  GLTexParameteri( context, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  MOSDBG( "TinyGL attach OK context=%08lx bitmap=%08lx tex=%lu %lux%lu depth=%lu",
          MOSPTR( context ), MOSPTR( bitmap ), (ULONG)v.gl_texture,
          (ULONG)v.width, (ULONG)v.height, depth );
  return 1;
#else
  return 0;
#endif
}

static int
present_tinygl( const libspectrum_word *pixels, unsigned pitch )
{
#ifdef __MORPHOS__
  struct Window *w = video_window();
  GLContext *context = (GLContext*)v.gl_context;
  unsigned rx, ry, rw, rh;

  if( !pixels || !context || !v.gl_bitmap || !w || !w->RPort ) return 0;
  convert_rgba( pixels, pitch );
  GLBindTexture( context, GL_TEXTURE_2D, v.gl_texture );
  if( !v.gl_texture_ready ) {
    GLTexImage2D( context, GL_TEXTURE_2D, 0, GL_RGBA,
                  v.source_width, v.source_height, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, v.rgba );
    v.gl_texture_ready = 1;
  } else {
    GLTexSubImage2D( context, GL_TEXTURE_2D, 0, 0, 0,
                     v.source_width, v.source_height,
                     GL_RGBA, GL_UNSIGNED_BYTE, v.rgba );
  }

  /* Clear the complete target to black, then render only the aspect-correct
     viewport. This produces black letterbox/pillarbox bars instead of MUI grey. */
  GLViewport( context, 0, 0, v.width, v.height );
  GLClearColor( context, 0, 0, 0, 1 );
  GLClear( context, GL_COLOR_BUFFER_BIT );
  morphosvideo_content_rect( &rx, &ry, &rw, &rh );
  GLViewport( context, rx, ry, rw, rh );
  GLMatrixMode( context, GL_PROJECTION );
  GLLoadIdentity( context );
  GLOrtho( context, 0, 1, 1, 0, -1, 1 );
  GLMatrixMode( context, GL_MODELVIEW );
  GLLoadIdentity( context );
  GLEnable( context, GL_TEXTURE_2D );
  GLBegin( context, GL_QUADS );
  GLTexCoord2f( context, 0, 0 ); GLVertex2f( context, 0, 0 );
  GLTexCoord2f( context, 1, 0 ); GLVertex2f( context, 1, 0 );
  GLTexCoord2f( context, 1, 1 ); GLVertex2f( context, 1, 1 );
  GLTexCoord2f( context, 0, 1 ); GLVertex2f( context, 0, 1 );
  GLEnd( context );
  GLFlush( context );
  GLASwapBuffers( context );

  BltBitMapRastPort( (struct BitMap*)v.gl_bitmap, 0, 0, w->RPort,
                     v.left, v.top, v.width, v.height, 0xc0 );
  return 1;
#else
  (void)pixels; (void)pitch;
  return 0;
#endif
}

static void
destroy_tinygl( void )
{
#ifdef __MORPHOS__
  if( v.gl_context ) {
    GLContext *context = (GLContext*)v.gl_context;
    MOSDBG( "TinyGL destroy context=%08lx bitmap=%08lx tex=%lu",
            MOSPTR( context ), MOSPTR( v.gl_bitmap ), (ULONG)v.gl_texture );
    if( v.gl_texture ) GLDeleteTextures( context, 1, &v.gl_texture );
    GLADestroyContext( context );
    GLClose( context );
  }
  if( v.gl_bitmap ) FreeBitMap( (struct BitMap*)v.gl_bitmap );
#endif
  v.gl_texture = 0;
  v.gl_texture_ready = 0;
  v.gl_context = NULL;
  v.gl_bitmap = NULL;
}

static int
attach_overlay( void )
{
#ifdef __MORPHOS__
  struct Window *w;
  struct VLayerHandle *layer;
  ULONG err = 0;
  ULONG fmt;

  if( !CGXVideoBase ) {
    MOSDBG( "overlay attach: cgxvideo.library unavailable" );
    return 0;
  }
  w = video_window();
  /* Follow the working openMSX MorphOS CGXVideo path here: once the native
     Window itself is valid, WScreen is the screen supplied to VLayer.
     TypeOfMem(WScreen) is an unnecessary extra rejection and can make a
     perfectly valid public/custom screen look unavailable. */
  if( !w || !w->WScreen ) {
    MOSDBG( "overlay attach: invalid window/screen win=%08lx screen=%08lx",
            MOSPTR( w ), MOSPTR( w ? w->WScreen : NULL ) );
    return 0;
  }
#if defined(SRCFMT_RGB16)
  fmt = SRCFMT_RGB16;
#elif defined(SRCFMT_R5G6B5PC)
  fmt = SRCFMT_R5G6B5PC;
#else
  MOSDBG( "overlay attach: SDK has no RGB565 source format" );
  return 0;
#endif

  MOSDBG( "overlay CreateVLayer screen=%08lx src=%lux%lu fmt=%08lx filter=%ld",
          MOSPTR( w->WScreen ), (ULONG)v.source_width, (ULONG)v.source_height,
          fmt, (LONG)v.linear_filter );
  layer = CreateVLayerHandleTags( w->WScreen,
      VOA_SrcType, fmt,
      VOA_SrcWidth, v.source_width,
      VOA_SrcHeight, v.source_height,
      VOA_DoubleBuffer, TRUE,
      VOA_UseFilter, v.linear_filter ? TRUE : FALSE,
      VOA_Error, (ULONG)&err,
      TAG_DONE );
  if( !layer ) {
    MOSDBG( "overlay CreateVLayer FAILED err=%lu", err );
    return 0;
  }

  v.vlayer = layer;
  v.overlay_geom_valid = 0;
  v.overlay_has_frame = 0;
  MOSDBG( "overlay layer=%08lx AttachVLayer win=%08lx", MOSPTR( layer ), MOSPTR( w ) );
  if( (LONG)AttachVLayerTags( layer, w,
      VOA_LeftIndent, 0UL, VOA_RightIndent, 0UL,
      VOA_TopIndent, 0UL, VOA_BottomIndent, 0UL, TAG_DONE ) != 0 ) {
    MOSDBG( "overlay AttachVLayer FAILED" );
    destroy_overlay();
    return 0;
  }
  update_overlay_geometry();
  MOSDBG( "overlay attach OK" );
  return 1;
#else
  return 0;
#endif
}

static void
update_overlay_geometry( void )
{
#ifdef __MORPHOS__
  struct Window *w;
  unsigned rx, ry, rw, rh;
  LONG inner_width, inner_height, inner_left, inner_top, right, bottom;

  if( !v.vlayer || !v.window ) return;
  w = video_window();
  if( !w ) return;
  morphosvideo_content_rect( &rx, &ry, &rw, &rh );

  inner_width = (LONG)w->Width - (LONG)w->BorderLeft - (LONG)w->BorderRight;
  inner_height = (LONG)w->Height - (LONG)w->BorderTop - (LONG)w->BorderBottom;
  if( inner_width < 1 ) inner_width = 1;
  if( inner_height < 1 ) inner_height = 1;
  inner_left = (LONG)v.left + (LONG)rx - (LONG)w->BorderLeft;
  inner_top = (LONG)v.top + (LONG)ry - (LONG)w->BorderTop;
  if( inner_left < 0 ) inner_left = 0;
  if( inner_top < 0 ) inner_top = 0;
  right = inner_width - ( inner_left + (LONG)rw );
  if( right < 0 ) right = 0;
  bottom = inner_height - ( inner_top + (LONG)rh );
  if( bottom < 0 ) bottom = 0;

  /* MUI may report the same geometry through both IDCMP_NEWSIZE and
     MUIM_Draw. Do not make cgxvideo.library process an identical VLayer
     update twice. Resize must only move/scale the existing overlay; it must
     never recreate or refill the source buffers. */
  if( v.overlay_geom_valid &&
      v.overlay_left_indent == inner_left &&
      v.overlay_right_indent == right &&
      v.overlay_top_indent == inner_top &&
      v.overlay_bottom_indent == bottom ) return;

  SetVLayerAttrTags( (struct VLayerHandle*)v.vlayer,
      VOA_LeftIndent, (ULONG)inner_left,
      VOA_RightIndent, (ULONG)right,
      VOA_TopIndent, (ULONG)inner_top,
      VOA_BottomIndent, (ULONG)bottom,
      TAG_DONE );
  v.overlay_left_indent = inner_left;
  v.overlay_right_indent = right;
  v.overlay_top_indent = inner_top;
  v.overlay_bottom_indent = bottom;
  v.overlay_geom_valid = 1;
  v.overlay_bars_dirty = 1;
#endif
}

static void
clear_overlay_bars( void )
{
#ifdef __MORPHOS__
  struct Window *w = video_window();
  unsigned rx, ry, rw, rh;
  UWORD base_left, base_top;
  if( !v.overlay_bars_dirty || !w || !w->RPort || !v.width || !v.height ) return;

  base_left = (UWORD)( v.left < 0 ? 0 : v.left );
  base_top = (UWORD)( v.top < 0 ? 0 : v.top );
  morphosvideo_content_rect( &rx, &ry, &rw, &rh );
  /* Only paint the uncovered strips. The old code repainted the complete
     video Area on every resize step, although CGXVideo itself owns the
     picture rectangle. */
  if( ry )
    FillPixelArray( w->RPort, base_left, base_top,
                    (UWORD)v.width, (UWORD)ry, 0xff000000UL );
  if( ry + rh < v.height )
    FillPixelArray( w->RPort, base_left, (UWORD)(base_top + ry + rh),
                    (UWORD)v.width, (UWORD)(v.height - ry - rh), 0xff000000UL );
  if( rx )
    FillPixelArray( w->RPort, base_left, (UWORD)(base_top + ry),
                    (UWORD)rx, (UWORD)rh, 0xff000000UL );
  if( rx + rw < v.width )
    FillPixelArray( w->RPort, (UWORD)(base_left + rx + rw), (UWORD)(base_top + ry),
                    (UWORD)(v.width - rx - rw), (UWORD)rh, 0xff000000UL );
  v.overlay_bars_dirty = 0;
#endif
}

int
morphosvideo_redraw_without_upload( void )
{
#ifdef __MORPHOS__
  /* Once one frame has reached the VLayer, a MUI redraw/interactive resize
     does not need another 320x240 upload, byte swap, buffer swap or WaitTOF.
     SetVLayerAttrTags() rescales the existing front buffer immediately. */
  if( v.active != MORPHOS_VIDEO_OVERLAY || !v.vlayer || !v.overlay_has_frame )
    return 0;
  clear_overlay_bars();
  return 1;
#else
  return 0;
#endif
}

static int
present_overlay( const libspectrum_word *pixels, unsigned pitch )
{
#ifdef __MORPHOS__
  struct VLayerHandle *layer = (struct VLayerHandle*)v.vlayer;
  struct Window *w = video_window();
  uint8_t *base;
  ULONG modulo;
  unsigned x, y;

  if( !pixels || !layer || !w ) return 0;

  clear_overlay_bars();

  if( !LockVLayer( layer ) ) {
    MOSDBG( "overlay LockVLayer FAILED" );
    return 0;
  }
  base = (uint8_t*)(APTR)GetVLayerAttr( layer, VOA_BaseAddress );
  modulo = GetVLayerAttr( layer, VOA_Modulo );
  if( !base || modulo < v.source_width * 2 ) {
    MOSDBG( "overlay invalid buffer base=%08lx modulo=%lu need=%lu",
            MOSPTR( base ), modulo, (ULONG)(v.source_width * 2) );
    UnlockVLayer( layer );
    return 0;
  }

  /* SRCFMT_RGB16 is R5G6B5PC (little-byte-first), while Fuse's native
     framebuffer is big-endian PPC RGB565.  G4/G5 use a VMX byte-permute
     directly into the locked VLayer; non-AltiVec CPUs retain the compact
     two-pixel 32-bit scalar path.  No scratch framebuffer or LUT is useful
     here: AltiVec has no gather and vec_perm performs the byte swap in one
     vector operation. */
  for( y = 0; y < v.source_height; y++ ) {
    const uint8_t *src = (const uint8_t*)pixels + (size_t)y * pitch;
    uint8_t *dst = base + (size_t)y * modulo;
    if( morphos_has_altivec && v.source_width >= 8 ) {
      morphosvideo_swap_rgb565le_altivec( (const uint16_t*)src, dst,
                                          v.source_width );
    } else if( !( ( (uintptr_t)src | (uintptr_t)dst ) & 3 ) ) {
      const uint32_t *s32 = (const uint32_t*)src;
      uint32_t *d32 = (uint32_t*)dst;
      unsigned pairs = v.source_width >> 1;
      unsigned i;
      for( i = 0; i < pairs; i++ ) {
        uint32_t q = s32[i];
        d32[i] = ( ( q & 0x00ff00ffU ) << 8 ) |
                 ( ( q & 0xff00ff00U ) >> 8 );
      }
      if( v.source_width & 1 ) {
        x = v.source_width - 1;
        dst[2*x+0] = src[2*x+1];
        dst[2*x+1] = src[2*x+0];
      }
    } else {
      for( x = 0; x < v.source_width; x++ ) {
        dst[2*x+0] = src[2*x+1];
        dst[2*x+1] = src[2*x+0];
      }
    }
  }
  UnlockVLayer( layer );
  /* Match the newer MorphOS presenter work: waiting for TOF here serializes
     the emulation thread. CGXVideo owns the double-buffered layer, so submit
     the ready back buffer immediately and let the driver schedule the swap. */
  SwapVLayerBuffer( layer );
  v.overlay_has_frame = 1;
  return 1;
#else
  (void)pixels; (void)pitch;
  return 0;
#endif
}

static void
destroy_overlay( void )
{
#ifdef __MORPHOS__
  if( v.vlayer ) {
    MOSDBG( "overlay destroy layer=%08lx", MOSPTR( v.vlayer ) );
    DetachVLayer( (struct VLayerHandle*)v.vlayer );
    DeleteVLayerHandle( (struct VLayerHandle*)v.vlayer );
  }
#endif
  v.vlayer = NULL;
  v.overlay_bars_dirty = 1;
  v.overlay_has_frame = 0;
  v.overlay_geom_valid = 0;
}

int
morphosvideo_present( const libspectrum_word *pixels, unsigned pitch )
{
  if( video_trace_present_count < 12 ) {
    MOSDBG( "video_present #%lu active=%s pixels=%08lx pitch=%lu window=%08lx layer=%08lx gl=%08lx",
            (ULONG)video_trace_present_count, morphosvideo_active_mode_name(),
            MOSPTR( pixels ), (ULONG)pitch, MOSPTR( v.window ), MOSPTR( v.vlayer ),
            MOSPTR( v.gl_context ) );
    video_trace_present_count++;
  }

  if( !pixels || !v.window ) return 0;
  if( v.active == MORPHOS_VIDEO_TINYGL ) {
    if( present_tinygl( pixels, pitch ) ) return 1;
    MOSDBG( "TinyGL present failed -> Overlay fallback" );
    destroy_tinygl();
    if( attach_overlay() ) {
      v.active = MORPHOS_VIDEO_OVERLAY;
      if( present_overlay( pixels, pitch ) ) return 1;
      destroy_overlay();
    }
    v.active = MORPHOS_VIDEO_SURFACE;
  } else if( v.active == MORPHOS_VIDEO_OVERLAY ) {
    if( present_overlay( pixels, pitch ) ) return 1;
    MOSDBG( "overlay present failed -> permanent Surface fallback" );
    destroy_overlay();
    v.active = MORPHOS_VIDEO_SURFACE;
  }
  return present_surface( pixels, pitch );
}

