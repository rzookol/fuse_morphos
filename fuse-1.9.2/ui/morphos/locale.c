/* locale.c - CatComp/locale.library support for the native MorphOS GUI.
   This intentionally mirrors the RDesktopGui locale implementation: the
   descriptor generates a CatCompArray and LOCSTR(MSG_xxx) indexes that array. */

#include "config.h"

#ifdef __MORPHOS__
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/locale.h>
#endif

#define CATCOMP_ARRAY
#define NEW_CATCOMP_ARRAY_IDS
#include "Fuse_strings.h"

#include "locale.h"

#ifdef __MORPHOS__
struct Library *LocaleBase = NULL;
static struct Catalog *locale_catalog;
#endif

void
locale_init( void )
{
#ifdef __MORPHOS__
  if( LocaleBase ) return;

  LocaleBase = OpenLibrary( "locale.library", 0 );
  if( LocaleBase ) {
    locale_catalog = OpenCatalog( NULL, (STRPTR)"Fuse.catalog",
                                  OC_BuiltInLanguage, (IPTR)"english",
                                  TAG_DONE );
  }
#endif
}

void
locale_cleanup( void )
{
#ifdef __MORPHOS__
  if( locale_catalog ) {
    CloseCatalog( locale_catalog );
    locale_catalog = NULL;
  }
  if( LocaleBase ) {
    CloseLibrary( LocaleBase );
    LocaleBase = NULL;
  }
#endif
}

const char *
locale_getstr( unsigned long array_id )
{
  const struct CatCompArrayType *t;
  const unsigned long count = sizeof( CatCompArray ) / sizeof( CatCompArray[0] );

  if( array_id >= count ) return "";
  t = CatCompArray + array_id;

#ifdef __MORPHOS__
  if( LocaleBase )
    return (const char*)GetCatalogStr( locale_catalog, (LONG)t->cca_ID,
                                      (STRPTR)t->cca_Str );
#endif
  return t->cca_Str;
}
