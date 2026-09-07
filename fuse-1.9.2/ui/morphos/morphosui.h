#ifndef FUSE_MORPHOS_UI_H
#define FUSE_MORPHOS_UI_H

#include <stddef.h>
#include "libspectrum.h"
#include "ui/ui.h"

int morphosui_set_video_size( unsigned width, unsigned height );
int morphosui_present( const libspectrum_word *pixels, unsigned width,
                       unsigned height, unsigned pitch );
void morphosui_redraw( void );
void morphosui_update_scaler_menu( void );
void morphosui_populate_machine_menu( void );
void morphosui_set_title_speed( float speed );
int morphosui_statusbar_update( ui_statusbar_item item, ui_statusbar_state state );
int morphosui_statusbar_speed( float speed );
void morphosui_statusbar_machine( const char *name );
int morphosui_menu_item_set_active( const char *path, int active );
int morphosui_set_fullscreen( int enabled );
int morphosui_error_request( ui_error_level severity, const char *message );
char *morphosui_request_file( const char *title, int save );
void morphosui_dialog_begin( void );
void morphosui_dialog_end( void );
void morphosui_widgets_reset( void );
int morphosui_debugger_activate( void );
int morphosui_debugger_deactivate( int interruptible );
int morphosui_debugger_update( void );
int morphosui_debugger_disassemble( libspectrum_word address );

#endif
