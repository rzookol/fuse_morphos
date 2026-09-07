/* morphosdisplay.c: native MorphOS display backend for Fuse */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "fuse.h"
#include "machine.h"
#include "settings.h"
#include "ui/scaler/scaler.h"
#include "ui/ui.h"
#include "ui/uidisplay.h"
#include "morphosui.h"
#include "morphosdisplay.h"
#include "morphosvideo.h"
#include "morphosdebug.h"

static libspectrum_word *source_pixels;
static libspectrum_word *scaled_pixels;
static libspectrum_word *saved_pixels;
static unsigned source_pitch;
static unsigned scaled_pitch;
static unsigned image_width;
static unsigned image_height;
static unsigned scaled_width;
static unsigned scaled_height;
static int dirty = 1;
static unsigned frame_trace_count;
static int morphos_border_enabled = 1;

static libspectrum_word colour_values[16];
static libspectrum_word bw_values[16];

static const unsigned char palette[16][3] = {
  {   0,   0,   0 }, {   0,   0, 192 }, { 192,   0,   0 }, { 192,   0, 192 },
  {   0, 192,   0 }, {   0, 192, 192 }, { 192, 192,   0 }, { 192, 192, 192 },
  {   0,   0,   0 }, {   0,   0, 255 }, { 255,   0,   0 }, { 255,   0, 255 },
  {   0, 255,   0 }, {   0, 255, 255 }, { 255, 255,   0 }, { 255, 255, 255 }
};

static libspectrum_word
rgb565( unsigned r, unsigned g, unsigned b )
{
  return (libspectrum_word)( ( ( r >> 3 ) << 11 ) |
                             ( ( g >> 2 ) << 5 ) |
                             ( b >> 3 ) );
}

static void
init_colours( void )
{
  unsigned i;
  for( i = 0; i < 16; i++ ) {
    unsigned r = palette[i][0], g = palette[i][1], b = palette[i][2];
    unsigned grey = (unsigned)( 0.299 * r + 0.587 * g + 0.114 * b + 0.5 );
    colour_values[i] = rgb565( r, g, b );
    bw_values[i] = rgb565( grey, grey, grey );
  }
}

static void
init_scalers( void )
{
  scaler_register_clear();
  scaler_register( SCALER_NORMAL );
  scaler_register( SCALER_2XSAI );
  scaler_register( SCALER_SUPER2XSAI );
  scaler_register( SCALER_SUPEREAGLE );
  scaler_register( SCALER_ADVMAME2X );
  scaler_register( SCALER_ADVMAME3X );
  scaler_register( SCALER_DOTMATRIX );
  scaler_register( SCALER_HQ2X );

  if( machine_current->timex ) {
    scaler_register( SCALER_HALF );
    scaler_register( SCALER_HALFSKIP );
    scaler_register( SCALER_TIMEXTV );
    scaler_register( SCALER_TIMEX1_5X );
    scaler_register( SCALER_TIMEX2X );
  } else {
    scaler_register( SCALER_DOUBLESIZE );
    scaler_register( SCALER_TRIPLESIZE );
    scaler_register( SCALER_QUADSIZE );
    scaler_register( SCALER_TV2X );
    scaler_register( SCALER_TV3X );
    scaler_register( SCALER_TV4X );
    scaler_register( SCALER_PALTV2X );
    scaler_register( SCALER_PALTV3X );
    scaler_register( SCALER_PALTV4X );
    scaler_register( SCALER_HQ3X );
    scaler_register( SCALER_HQ4X );
    scaler_register( SCALER_NTSC2X );
    scaler_register( SCALER_NTSC3X );
    scaler_register( SCALER_NTSC4X );
  }

  /* Initial setup must not call scaler_select_scaler(): that function
     intentionally calls uidisplay_hotswap_gfx_mode() after activating the
     scaler. Calling it from here would recurse back into the display hook. */
  if( scaler_is_supported( current_scaler ) )
    scaler_activate_scaler( current_scaler );
  else
    scaler_activate_scaler( SCALER_NORMAL );
  scaler_select_bitformat( 565 );
  morphosui_update_scaler_menu();
}

static void
morphosdisplay_visible_geometry( unsigned *left, unsigned *top,
                                 unsigned *width, unsigned *height )
{
  unsigned scale = ( machine_current && machine_current->timex ) ? 2 : 1;
  unsigned bx = DISPLAY_BORDER_ASPECT_WIDTH * scale;
  unsigned by = DISPLAY_BORDER_HEIGHT * scale;

  *left = *top = 0;
  *width = image_width;
  *height = image_height;
  if( morphos_border_enabled ) return;

  if( image_width > 2 * bx && image_height > 2 * by ) {
    *left = bx;
    *top = by;
    *width = image_width - 2 * bx;
    *height = image_height - 2 * by;
  }
}

int
morphosdisplay_border_enabled( void )
{
  return morphos_border_enabled;
}

int
morphosdisplay_set_border( int enabled )
{
  int old = morphos_border_enabled;
  enabled = !!enabled;
  if( enabled == old ) return 0;
  morphos_border_enabled = enabled;
  if( display_ui_initialised && uidisplay_hotswap_gfx_mode() ) {
    morphos_border_enabled = old;
    uidisplay_hotswap_gfx_mode();
    return 1;
  }
  return 0;
}

static int
allocate_buffers( void )
{
  float factor = scaler_get_scaling_factor( current_scaler );
  size_t source_count, scaled_count;
  unsigned visible_left, visible_top, visible_width, visible_height;

  morphosdisplay_visible_geometry( &visible_left, &visible_top,
                                   &visible_width, &visible_height );
  (void)visible_left; (void)visible_top;
  if( factor <= 0.0f ) factor = 1.0f;
  scaled_width = (unsigned)( visible_width * factor + 0.5f );
  scaled_height = (unsigned)( visible_height * factor + 0.5f );
  if( !scaled_width ) scaled_width = visible_width;
  if( !scaled_height ) scaled_height = visible_height;

  source_pitch = ( image_width + 3 ) * sizeof( libspectrum_word );
  scaled_pitch = scaled_width * sizeof( libspectrum_word );
  source_count = (size_t)( image_width + 3 ) * ( image_height + 3 );
  scaled_count = (size_t)scaled_width * scaled_height;

  MOSDBG( "display allocate image=%lux%lu scaled=%lux%lu source_count=%lu scaled_count=%lu",
          (ULONG)image_width, (ULONG)image_height, (ULONG)scaled_width, (ULONG)scaled_height,
          (ULONG)source_count, (ULONG)scaled_count );
  free( source_pixels ); source_pixels = calloc( source_count, sizeof( *source_pixels ) );
  free( scaled_pixels ); scaled_pixels = calloc( scaled_count, sizeof( *scaled_pixels ) );
  MOSDBG( "display buffers source=%08lx scaled=%08lx source_pitch=%lu scaled_pitch=%lu",
          MOSPTR( source_pixels ), MOSPTR( scaled_pixels ), (ULONG)source_pitch, (ULONG)scaled_pitch );
  if( !source_pixels || !scaled_pixels ) return 1;

  dirty = 1;
  return morphosui_set_video_size( scaled_width, scaled_height );
}

int
uidisplay_init( int width, int height )
{
  MOSDBG( "uidisplay_init %ldx%ld", (LONG)width, (LONG)height );
  image_width = width;
  image_height = height;
  init_scalers();
  init_colours();
  if( allocate_buffers() ) {
    ui_error( UI_ERROR_ERROR, "MorphOS: cannot allocate display buffers" );
    return 1;
  }
  display_ui_initialised = 1;
  display_refresh_all();
  MOSDBG( "uidisplay_init OK" );
  return 0;
}

int
uidisplay_hotswap_gfx_mode( void )
{
  MOSDBG( "hotswap begin scaler=%ld fullscreen=%ld",
          (LONG)current_scaler, (LONG)settings_current.full_screen );
  fuse_emulation_pause();
  /* MorphOS fullscreen uses a dedicated Intuition Screen. Surface follows
     the scaler/filter output size; Overlay/TinyGL clone Ambient. */
  morphosui_set_fullscreen( settings_current.full_screen );

  /* scaler_select_scaler() calls this function after scaler_activate_scaler().
     Never call scaler_select_scaler() from the hotswap callback itself or we
     get an infinite uidisplay_hotswap_gfx_mode <-> scaler_select_scaler loop. */
  if( !scaler_is_supported( current_scaler ) &&
      scaler_activate_scaler( SCALER_NORMAL ) ) {
    fuse_emulation_unpause();
    return 1;
  }

  scaler_select_bitformat( 565 );
  if( allocate_buffers() ) {
    fuse_emulation_unpause();
    return 1;
  }
  display_refresh_all();
  morphosui_update_scaler_menu();
  fuse_emulation_unpause();
  MOSDBG( "hotswap done scaler=%ld size=%lux%lu",
          (LONG)current_scaler, (ULONG)scaled_width, (ULONG)scaled_height );
  return 0;
}

void
uidisplay_putpixel( int x, int y, int colour )
{
  libspectrum_word *palette_values = settings_current.bw_tv ? bw_values : colour_values;
  libspectrum_word c = palette_values[colour & 15];

  if( machine_current->timex ) {
    libspectrum_word *p;
    x <<= 1; y <<= 1;
    p = (libspectrum_word*)( (libspectrum_byte*)source_pixels +
                            ( y + 1 ) * source_pitch ) + x + 1;
    p[0] = p[1] = c;
    p = (libspectrum_word*)( (libspectrum_byte*)p + source_pitch );
    p[0] = p[1] = c;
  } else {
    libspectrum_word *p = (libspectrum_word*)( (libspectrum_byte*)source_pixels +
                             ( y + 1 ) * source_pitch ) + x + 1;
    *p = c;
  }
  dirty = 1;
}

void
uidisplay_plot8( int x, int y, libspectrum_byte data,
                 libspectrum_byte ink, libspectrum_byte paper )
{
  libspectrum_word *palette_values = settings_current.bw_tv ? bw_values : colour_values;
  libspectrum_word ci = palette_values[ink & 15], cp = palette_values[paper & 15];
  libspectrum_word *dest;
  int i, yy;

  if( machine_current->timex ) {
    x <<= 4; y <<= 1;
    for( yy = 0; yy < 2; yy++ ) {
      dest = (libspectrum_word*)( (libspectrum_byte*)source_pixels +
                                  ( y + yy + 1 ) * source_pitch ) + x + 1;
      for( i = 0; i < 8; i++ ) {
        libspectrum_word c = ( data & ( 0x80 >> i ) ) ? ci : cp;
        *dest++ = c; *dest++ = c;
      }
    }
  } else {
    x <<= 3;
    dest = (libspectrum_word*)( (libspectrum_byte*)source_pixels +
                                ( y + 1 ) * source_pitch ) + x + 1;
    for( i = 0; i < 8; i++ ) *dest++ = ( data & ( 0x80 >> i ) ) ? ci : cp;
  }
  dirty = 1;
}

void
uidisplay_plot16( int x, int y, libspectrum_word data,
                  libspectrum_byte ink, libspectrum_byte paper )
{
  libspectrum_word *palette_values = settings_current.bw_tv ? bw_values : colour_values;
  libspectrum_word ci = palette_values[ink & 15], cp = palette_values[paper & 15];
  libspectrum_word *dest;
  int i, yy;

  x <<= 4; y <<= 1;
  for( yy = 0; yy < 2; yy++ ) {
    dest = (libspectrum_word*)( (libspectrum_byte*)source_pixels +
                                ( y + yy + 1 ) * source_pitch ) + x + 1;
    for( i = 0; i < 16; i++ )
      *dest++ = ( data & ( 0x8000 >> i ) ) ? ci : cp;
  }
  dirty = 1;
}

void
uidisplay_area( int x, int y, int width, int height )
{
  (void)x; (void)y; (void)width; (void)height;
  dirty = 1;
}

void
uidisplay_frame_end( void )
{
  const libspectrum_word *native_pixels;
  unsigned visible_left, visible_top, visible_width, visible_height;

  if( !source_pixels || !scaled_pixels ) return;
  if( !dirty && ui_widget_level < 0 ) return;

  morphosdisplay_visible_geometry( &visible_left, &visible_top,
                                   &visible_width, &visible_height );
  native_pixels = (const libspectrum_word*)
    ( (const libspectrum_byte*)source_pixels +
      (size_t)( visible_top + 1 ) * source_pitch +
      (size_t)( visible_left + 1 ) * sizeof( libspectrum_word ) );

  if( morphosvideo_active_mode() != MORPHOS_VIDEO_SURFACE ) {
    /* Accelerated renderers receive only the visible source rectangle.
       Border-off is therefore a zero-copy crop: the pointer is advanced into
       the existing RGB565 framebuffer and the original row pitch is kept. */
    if( frame_trace_count < 12 )
      MOSDBG( "frame_end #%lu %s-HWSCALE native=%08lx %lux%lu pitch=%lu border=%ld",
              (ULONG)frame_trace_count, morphosvideo_active_mode_name(),
              MOSPTR( native_pixels ), (ULONG)visible_width, (ULONG)visible_height,
              (ULONG)source_pitch, (LONG)morphos_border_enabled );
    morphosui_present( native_pixels, visible_width, visible_height, source_pitch );
  } else {
    scaler_proc16( (libspectrum_byte*)native_pixels, source_pitch,
                   (libspectrum_byte*)scaled_pixels, scaled_pitch,
                   visible_width, visible_height );

    if( frame_trace_count < 12 )
      MOSDBG( "frame_end #%lu SURFACE-SWSCALE scaled=%08lx %lux%lu pitch=%lu border=%ld",
              (ULONG)frame_trace_count, MOSPTR( scaled_pixels ),
              (ULONG)scaled_width, (ULONG)scaled_height, (ULONG)scaled_pitch,
              (LONG)morphos_border_enabled );
    morphosui_present( scaled_pixels, scaled_width, scaled_height, scaled_pitch );
  }

  if( frame_trace_count < 12 ) frame_trace_count++;
  dirty = 0;
}

#ifdef USE_WIDGET
void
uidisplay_frame_save( void )
{
  size_t count = (size_t)( image_width + 3 ) * ( image_height + 3 );
  free( saved_pixels );
  saved_pixels = malloc( count * sizeof( *saved_pixels ) );
  if( saved_pixels ) memcpy( saved_pixels, source_pixels, count * sizeof( *saved_pixels ) );
}

void
uidisplay_frame_restore( void )
{
  size_t count = (size_t)( image_width + 3 ) * ( image_height + 3 );
  if( saved_pixels && source_pixels ) {
    memcpy( source_pixels, saved_pixels, count * sizeof( *saved_pixels ) );
    dirty = 1;
  }
}
#endif

int
uidisplay_end( void )
{
  MOSDBG( "uidisplay_end begin source=%08lx scaled=%08lx", MOSPTR( source_pixels ), MOSPTR( scaled_pixels ) );
  display_ui_initialised = 0;
  free( source_pixels ); source_pixels = NULL;
  free( scaled_pixels ); scaled_pixels = NULL;
  free( saved_pixels ); saved_pixels = NULL;
  dirty = 1;
  MOSDBG( "uidisplay_end done" );
  return 0;
}
