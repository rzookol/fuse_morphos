/* paths.c: Path-related compatibility routines
   Copyright (c) 1999-2012 Philip Kendall

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"

#include <errno.h>
#include <stdio.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif				/* #ifdef HAVE_LIBGEN_H */
#include <string.h>
#include <unistd.h>

#include "compat.h"
#include "fuse.h"
#include "ui/ui.h"

const char*
compat_get_temp_path( void )
{
  return "T:";
}

const char*
compat_get_config_path( void )
{
  return "PROGDIR:settings";
}

const char*
compat_get_fallback_config_path( void )
{
  return NULL;
}

int
compat_is_absolute_path( const char *path )
{
  return strchr( path, ':' ) != NULL;
}

int
compat_get_next_path( path_context *ctx )
{
  const char *path2;

  switch( (ctx->state)++ ) {

  case 0:
    /* MorphOS programs normally keep private data relative to PROGDIR:. */
    snprintf( ctx->path, PATH_MAX, "%s", "PROGDIR:" );
    return 1;

  case 1:
    switch( ctx->type ) {
    case UTILS_AUXILIARY_LIB:
      snprintf( ctx->path, PATH_MAX, "%s", "PROGDIR:lib/" );
      return 1;
    case UTILS_AUXILIARY_ROM:
      snprintf( ctx->path, PATH_MAX, "%s", "PROGDIR:roms/" );
      return 1;
    case UTILS_AUXILIARY_WIDGET:
      snprintf( ctx->path, PATH_MAX, "%s", "PROGDIR:ui/widget/" );
      return 1;
    default:
      ui_error( UI_ERROR_ERROR, "unknown auxiliary file type %d", ctx->type );
      return 0;
    }

  case 2:
#ifndef ROMSDIR
    path2 = FUSEDATADIR;
#else
    path2 = ctx->type == UTILS_AUXILIARY_ROM ? ROMSDIR : FUSEDATADIR;
#endif
    snprintf( ctx->path, PATH_MAX, "%s", path2 );
    return 1;

  case 3:
    return 0;
  }

  ui_error( UI_ERROR_ERROR, "unknown path_context state %d", ctx->state );
  fuse_abort();
}
