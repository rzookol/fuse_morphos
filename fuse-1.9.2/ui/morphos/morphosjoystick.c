/* morphosjoystick.c: MorphOS lowlevel.library joystick backend for Fuse
   Modelled after the native openMSX MorphOS joystick glue, adapted to Fuse's
   input_event layer.  lowlevel.library is optional and is polled so USB/CD32
   controller hot-plugging works without restarting the emulator. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MORPHOS__
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>
#endif

#include "input.h"
#include "ui/uijoystick.h"
#include "ui/morphos/morphosjoystick.h"

#ifdef __MORPHOS__
struct Library *LowLevelBase = NULL;
#endif

#define MORPHOS_LOGICAL_JOYSTICKS 2
#define MORPHOS_LOWLEVEL_PORTS    4

/* Fuse logical joystick 0/1 -> MorphOS lowlevel.library port. */
static int port_map[MORPHOS_LOGICAL_JOYSTICKS] = { 0, 1 };
static unsigned long previous_state[MORPHOS_LOGICAL_JOYSTICKS];
static int previous_valid[MORPHOS_LOGICAL_JOYSTICKS];
static int prefs_loaded;

static int
valid_logical( int which )
{
  return which >= 0 && which < MORPHOS_LOGICAL_JOYSTICKS;
}

static int
valid_port( int port )
{
  return port >= 0 && port < MORPHOS_LOWLEVEL_PORTS;
}

#ifdef __MORPHOS__
static int
ensure_lowlevel( void )
{
  if( LowLevelBase ) return 1;
  LowLevelBase = OpenLibrary( "lowlevel.library", 0 );
  return LowLevelBase != NULL;
}

static int
usable_type( ULONG state )
{
  ULONG type = state & JP_TYPE_MASK;
  return type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK ||
         type == JP_TYPE_UNKNOWN;
}
#else
static int ensure_lowlevel( void ) { return 0; }
#endif

static void
send_event( int which, input_key button, int pressed )
{
  input_event_t event;
  memset( &event, 0, sizeof( event ) );
  event.type = pressed ? INPUT_EVENT_JOYSTICK_PRESS :
                         INPUT_EVENT_JOYSTICK_RELEASE;
  event.types.joystick.which = which;
  event.types.joystick.button = button;
  input_event( &event );
}

static void
release_all( int which )
{
  static const input_key buttons[] = {
    INPUT_JOYSTICK_UP, INPUT_JOYSTICK_DOWN,
    INPUT_JOYSTICK_LEFT, INPUT_JOYSTICK_RIGHT,
    INPUT_JOYSTICK_FIRE_1, INPUT_JOYSTICK_FIRE_2,
    INPUT_JOYSTICK_FIRE_3, INPUT_JOYSTICK_FIRE_4,
    INPUT_JOYSTICK_FIRE_5, INPUT_JOYSTICK_FIRE_6,
    INPUT_JOYSTICK_FIRE_7
  };
  size_t i;

  if( !valid_logical( which ) ) return;
  if( previous_valid[which] ) {
    for( i = 0; i < sizeof( buttons ) / sizeof( buttons[0] ); ++i )
      send_event( which, buttons[i], 0 );
  }
  previous_state[which] = 0;
  previous_valid[which] = 0;
}

#ifdef __MORPHOS__
typedef struct morphos_joy_bit {
  ULONG mask;
  input_key key;
} morphos_joy_bit;

static const morphos_joy_bit joy_bits[] = {
  { JPF_JOY_UP,         INPUT_JOYSTICK_UP },
  { JPF_JOY_DOWN,       INPUT_JOYSTICK_DOWN },
  { JPF_JOY_LEFT,       INPUT_JOYSTICK_LEFT },
  { JPF_JOY_RIGHT,      INPUT_JOYSTICK_RIGHT },

  /* Same convention as the openMSX MorphOS port: RED is primary fire and
     BLUE is secondary.  Fuse can make useful use of the extra CD32 buttons,
     so expose those as Fire 3..7 as well. */
  { JPF_BUTTON_RED,     INPUT_JOYSTICK_FIRE_1 },
  { JPF_BUTTON_BLUE,    INPUT_JOYSTICK_FIRE_2 },
  { JPF_BUTTON_YELLOW,  INPUT_JOYSTICK_FIRE_3 },
  { JPF_BUTTON_GREEN,   INPUT_JOYSTICK_FIRE_4 },
  { JPF_BUTTON_FORWARD, INPUT_JOYSTICK_FIRE_5 },
  { JPF_BUTTON_REVERSE, INPUT_JOYSTICK_FIRE_6 },
  { JPF_BUTTON_PLAY,    INPUT_JOYSTICK_FIRE_7 }
};
#endif

void
morphosjoystick_load_preferences( void )
{
  FILE *f;
  char line[128];

  if( prefs_loaded ) return;
  prefs_loaded = 1;

  f = fopen( "ENV:Fuse_MorphOS_prefs", "r" );
  if( !f ) f = fopen( "ENVARC:Fuse_MorphOS_prefs", "r" );
  if( !f ) return;

  while( fgets( line, sizeof( line ), f ) ) {
    char *eq = strchr( line, '=' );
    char *end;
    long value;
    int which = -1;

    if( !eq ) continue;
    *eq++ = 0;
    eq[strcspn( eq, "\r\n" )] = 0;

    if( !strcmp( line, "joy1_port" ) ) which = 0;
    else if( !strcmp( line, "joy2_port" ) ) which = 1;
    else continue;

    value = strtol( eq, &end, 10 );
    if( end != eq && valid_port( (int)value ) ) port_map[which] = (int)value;
  }
  fclose( f );
}

int
morphosjoystick_available( void )
{
#ifdef __MORPHOS__
  return ensure_lowlevel();
#else
  return 0;
#endif
}

int
morphosjoystick_get_port( int which )
{
  morphosjoystick_load_preferences();
  if( !valid_logical( which ) ) return 0;
  return port_map[which];
}

void
morphosjoystick_set_port( int which, int port )
{
  if( !valid_logical( which ) || !valid_port( port ) ) return;
  morphosjoystick_load_preferences();
  if( port_map[which] == port ) return;

  /* Release the old mapping before switching so changing a gamepad while a
     direction/button is held cannot leave a Spectrum joystick bit stuck. */
  release_all( which );
  port_map[which] = port;
}

int
morphosjoystick_port_connected( int port )
{
#ifdef __MORPHOS__
  ULONG state;
  if( !valid_port( port ) || !ensure_lowlevel() ) return 0;
  state = ReadJoyPort( (ULONG)port );
  return usable_type( state );
#else
  (void)port;
  return 0;
#endif
}

int
ui_joystick_init( void )
{
  morphosjoystick_load_preferences();
  memset( previous_state, 0, sizeof( previous_state ) );
  memset( previous_valid, 0, sizeof( previous_valid ) );

  /* As in openMSX, don't require a controller to be connected during init.
     Keeping the logical devices alive makes lowlevel/Poseidon hot-plug work. */
  return morphosjoystick_available() ? MORPHOS_LOGICAL_JOYSTICKS : 0;
}

void
ui_joystick_poll( void )
{
#ifdef __MORPHOS__
  int which;
  if( !LowLevelBase ) return;

  for( which = 0; which < MORPHOS_LOGICAL_JOYSTICKS; ++which ) {
    ULONG state = ReadJoyPort( (ULONG)port_map[which] );
    ULONG old = previous_state[which];
    size_t i;

    if( !usable_type( state ) ) {
      release_all( which );
      continue;
    }

    if( !previous_valid[which] ) old = 0;

    for( i = 0; i < sizeof( joy_bits ) / sizeof( joy_bits[0] ); ++i ) {
      int was = ( old & joy_bits[i].mask ) != 0;
      int now = ( state & joy_bits[i].mask ) != 0;
      if( was != now ) send_event( which, joy_bits[i].key, now );
    }

    previous_state[which] = state;
    previous_valid[which] = 1;
  }
#endif
}

void
ui_joystick_end( void )
{
  int which;
  for( which = 0; which < MORPHOS_LOGICAL_JOYSTICKS; ++which )
    release_all( which );

#ifdef __MORPHOS__
  if( LowLevelBase ) {
    CloseLibrary( LowLevelBase );
    LowLevelBase = NULL;
  }
#endif
}
