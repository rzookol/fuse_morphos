#ifndef FUSE_MORPHOS_KEYBOARD_H
#define FUSE_MORPHOS_KEYBOARD_H

#include <exec/types.h>

void morphoskeyboard_key( UWORD rawcode, UWORD qualifier, int down );
void morphoskeyboard_release_all( void );

#endif
