#ifndef FUSE_MORPHOS_JOYSTICK_H
#define FUSE_MORPHOS_JOYSTICK_H

/* Native MorphOS lowlevel.library host joystick backend.
   Fuse exposes two logical host joysticks; each can be mapped to any of the
   four lowlevel.library ports, matching the native openMSX MorphOS port. */

int morphosjoystick_available( void );
int morphosjoystick_get_port( int which );
void morphosjoystick_set_port( int which, int port );
int morphosjoystick_port_connected( int port );
void morphosjoystick_load_preferences( void );

#endif
