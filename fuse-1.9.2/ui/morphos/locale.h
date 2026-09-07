#ifndef FUSE_MORPHOS_LOCALE_H
#define FUSE_MORPHOS_LOCALE_H

#define NEW_CATCOMP_ARRAY_IDS
#include "Fuse_strings.h"

void locale_init( void );
void locale_cleanup( void );
const char *locale_getstr( unsigned long array_id );

#define LOCSTR(x) locale_getstr( x##_ID )

#endif /* FUSE_MORPHOS_LOCALE_H */
