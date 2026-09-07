#ifndef FUSE_MORPHOS_VIDEO_H
#define FUSE_MORPHOS_VIDEO_H

#include <libspectrum.h>

typedef enum morphos_video_mode {
  MORPHOS_VIDEO_SURFACE = 0,
  MORPHOS_VIDEO_OVERLAY = 1,
  MORPHOS_VIDEO_TINYGL = 2
} morphos_video_mode;

int morphosvideo_init( void );
void morphosvideo_end( void );
int morphosvideo_configure( unsigned source_width, unsigned source_height,
                           unsigned source_pitch );
int morphosvideo_attach( void *window, int left, int top,
                         unsigned width, unsigned height );
void morphosvideo_detach( void );
void morphosvideo_set_geometry( int left, int top,
                                unsigned width, unsigned height );
int morphosvideo_present( const libspectrum_word *pixels, unsigned pitch );
int morphosvideo_redraw_without_upload( void );
int morphosvideo_set_mode( morphos_video_mode mode );
morphos_video_mode morphosvideo_requested_mode( void );
morphos_video_mode morphosvideo_active_mode( void );
const char *morphosvideo_active_mode_name( void );
int morphosvideo_has_overlay( void );
int morphosvideo_has_tinygl( void );
void morphosvideo_set_vsync( int enabled );
void morphosvideo_set_linear_filter( int enabled );
void morphosvideo_set_fullscreen( int enabled );
int morphosvideo_vsync( void );
int morphosvideo_linear_filter( void );
void morphosvideo_content_rect( unsigned *x, unsigned *y,
                                unsigned *w, unsigned *h );

#endif
