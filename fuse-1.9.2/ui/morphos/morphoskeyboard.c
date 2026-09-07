/* morphoskeyboard.c: native MorphOS keyboard input for Fuse */
#include "config.h"

#ifdef __MORPHOS__
#include <devices/rawkeycodes.h>
#include <intuition/intuition.h>
#endif

#include <string.h>

#include "input.h"
#include "keyboard.h"
#include "morphoskeyboard.h"

static input_key pressed[128];

/*
 * The native MorphOS input backend translates RAWKEY_* values directly to
 * Fuse input_key values in raw_to_key(), so it does not use keysyms_remap().
 * keyboard.c nevertheless always builds the UI keysym hash at startup and
 * therefore requires this symbol to exist.  An empty map is correct here.
 */
keysyms_map_t keysyms_map[] = {
  { 0, INPUT_KEY_NONE }
};

static input_key
raw_to_key( unsigned raw )
{
#ifdef __MORPHOS__
  switch( raw ) {
  case RAWKEY_TILDE:       return INPUT_KEY_asciitilde;
  case RAWKEY_1:           return INPUT_KEY_1;
  case RAWKEY_2:           return INPUT_KEY_2;
  case RAWKEY_3:           return INPUT_KEY_3;
  case RAWKEY_4:           return INPUT_KEY_4;
  case RAWKEY_5:           return INPUT_KEY_5;
  case RAWKEY_6:           return INPUT_KEY_6;
  case RAWKEY_7:           return INPUT_KEY_7;
  case RAWKEY_8:           return INPUT_KEY_8;
  case RAWKEY_9:           return INPUT_KEY_9;
  case RAWKEY_0:           return INPUT_KEY_0;
  case RAWKEY_MINUS:       return INPUT_KEY_minus;
  case RAWKEY_EQUAL:       return INPUT_KEY_equal;
  case RAWKEY_BACKSLASH:   return INPUT_KEY_backslash;

  case RAWKEY_Q: return INPUT_KEY_q;
  case RAWKEY_W: return INPUT_KEY_w;
  case RAWKEY_E: return INPUT_KEY_e;
  case RAWKEY_R: return INPUT_KEY_r;
  case RAWKEY_T: return INPUT_KEY_t;
  case RAWKEY_Y: return INPUT_KEY_y;
  case RAWKEY_U: return INPUT_KEY_u;
  case RAWKEY_I: return INPUT_KEY_i;
  case RAWKEY_O: return INPUT_KEY_o;
  case RAWKEY_P: return INPUT_KEY_p;
  case RAWKEY_LBRACKET:    return INPUT_KEY_bracketleft;
  case RAWKEY_RBRACKET:    return INPUT_KEY_bracketright;

  case RAWKEY_A: return INPUT_KEY_a;
  case RAWKEY_S: return INPUT_KEY_s;
  case RAWKEY_D: return INPUT_KEY_d;
  case RAWKEY_F: return INPUT_KEY_f;
  case RAWKEY_G: return INPUT_KEY_g;
  case RAWKEY_H: return INPUT_KEY_h;
  case RAWKEY_J: return INPUT_KEY_j;
  case RAWKEY_K: return INPUT_KEY_k;
  case RAWKEY_L: return INPUT_KEY_l;
  case RAWKEY_SEMICOLON:   return INPUT_KEY_semicolon;
  case RAWKEY_QUOTE:       return INPUT_KEY_apostrophe;

  case RAWKEY_LESSGREATER: return INPUT_KEY_less;
  case RAWKEY_Z: return INPUT_KEY_z;
  case RAWKEY_X: return INPUT_KEY_x;
  case RAWKEY_C: return INPUT_KEY_c;
  case RAWKEY_V: return INPUT_KEY_v;
  case RAWKEY_B: return INPUT_KEY_b;
  case RAWKEY_N: return INPUT_KEY_n;
  case RAWKEY_M: return INPUT_KEY_m;
  case RAWKEY_COMMA:       return INPUT_KEY_comma;
  case RAWKEY_PERIOD:      return INPUT_KEY_period;
  case RAWKEY_SLASH:       return INPUT_KEY_slash;

  case RAWKEY_SPACE:       return INPUT_KEY_space;
  case RAWKEY_BACKSPACE:   return INPUT_KEY_BackSpace;
  case RAWKEY_TAB:         return INPUT_KEY_Tab;
  case RAWKEY_KP_ENTER:    return INPUT_KEY_KP_Enter;
  case RAWKEY_RETURN:      return INPUT_KEY_Return;
  case RAWKEY_ESCAPE:      return INPUT_KEY_Escape;
  case RAWKEY_DELETE:      return INPUT_KEY_Delete;
  case RAWKEY_INSERT:      return INPUT_KEY_Insert;
  case RAWKEY_PAGEUP:      return INPUT_KEY_Page_Up;
  case RAWKEY_PAGEDOWN:    return INPUT_KEY_Page_Down;
  case RAWKEY_UP:          return INPUT_KEY_Up;
  case RAWKEY_DOWN:        return INPUT_KEY_Down;
  case RAWKEY_RIGHT:       return INPUT_KEY_Right;
  case RAWKEY_LEFT:        return INPUT_KEY_Left;
  case RAWKEY_HOME:        return INPUT_KEY_Home;
  case RAWKEY_END:         return INPUT_KEY_End;

  case RAWKEY_F1:  return INPUT_KEY_F1;
  case RAWKEY_F2:  return INPUT_KEY_F2;
  case RAWKEY_F3:  return INPUT_KEY_F3;
  case RAWKEY_F4:  return INPUT_KEY_F4;
  case RAWKEY_F5:  return INPUT_KEY_F5;
  case RAWKEY_F6:  return INPUT_KEY_F6;
  case RAWKEY_F7:  return INPUT_KEY_F7;
  case RAWKEY_F8:  return INPUT_KEY_F8;
  case RAWKEY_F9:  return INPUT_KEY_F9;
  case RAWKEY_F10: return INPUT_KEY_F10;
  case RAWKEY_F11: return INPUT_KEY_F11;
  case RAWKEY_F12: return INPUT_KEY_F12;

  case RAWKEY_LSHIFT:      return INPUT_KEY_Shift_L;
  case RAWKEY_RSHIFT:      return INPUT_KEY_Shift_R;
  case RAWKEY_CONTROL:     return INPUT_KEY_Control_L;
  case RAWKEY_LALT:        return INPUT_KEY_Alt_L;
  case RAWKEY_RALT:        return INPUT_KEY_Alt_R;
  case RAWKEY_LAMIGA:      return INPUT_KEY_Meta_L;
  case RAWKEY_RAMIGA:      return INPUT_KEY_Meta_R;
  case RAWKEY_CAPSLOCK:    return INPUT_KEY_Caps_Lock;

  case RAWKEY_KP_0:        return INPUT_KEY_0;
  case RAWKEY_KP_1:        return INPUT_KEY_1;
  case RAWKEY_KP_2:        return INPUT_KEY_2;
  case RAWKEY_KP_3:        return INPUT_KEY_3;
  case RAWKEY_KP_4:        return INPUT_KEY_4;
  case RAWKEY_KP_5:        return INPUT_KEY_5;
  case RAWKEY_KP_6:        return INPUT_KEY_6;
  case RAWKEY_KP_7:        return INPUT_KEY_7;
  case RAWKEY_KP_8:        return INPUT_KEY_8;
  case RAWKEY_KP_9:        return INPUT_KEY_9;
  case RAWKEY_KP_DECIMAL:  return INPUT_KEY_period;
  case RAWKEY_KP_MINUS:    return INPUT_KEY_minus;
  case RAWKEY_KP_DIVIDE:   return INPUT_KEY_slash;
  case RAWKEY_KP_MULTIPLY: return INPUT_KEY_asterisk;
  case RAWKEY_KP_PLUS:     return INPUT_KEY_plus;
  default:                  return INPUT_KEY_NONE;
  }
#else
  (void)raw;
  return INPUT_KEY_NONE;
#endif
}

void
morphoskeyboard_key( UWORD rawcode, UWORD qualifier, int down )
{
  input_event_t event;
  input_key key;
  unsigned raw = rawcode & 0x7f;

  (void)qualifier;
  if( raw >= 128 ) return;

  if( down ) {
    key = raw_to_key( raw );
    if( key == INPUT_KEY_NONE ) return;
    pressed[ raw ] = key;
    event.type = INPUT_EVENT_KEYPRESS;
  } else {
    key = pressed[ raw ];
    if( key == INPUT_KEY_NONE ) key = raw_to_key( raw );
    if( key == INPUT_KEY_NONE ) return;
    pressed[ raw ] = INPUT_KEY_NONE;
    event.type = INPUT_EVENT_KEYRELEASE;
  }

  event.types.key.native_key = key;
  event.types.key.spectrum_key = key;
  input_event( &event );
}

void
morphoskeyboard_release_all( void )
{
  unsigned i;
  input_event_t event;

  for( i = 0; i < 128; i++ ) {
    if( pressed[i] == INPUT_KEY_NONE ) continue;
    event.type = INPUT_EVENT_KEYRELEASE;
    event.types.key.native_key = pressed[i];
    event.types.key.spectrum_key = pressed[i];
    input_event( &event );
    pressed[i] = INPUT_KEY_NONE;
  }
}
