/* morphosdebug.h: lightweight MorphOS serial/debug-log tracing. */
#ifndef FUSE_UI_MORPHOS_DEBUG_H
#define FUSE_UI_MORPHOS_DEBUG_H

#ifdef __MORPHOS__
#include <exec/types.h>
#define MOSPTR(p) ((ULONG)(IPTR)(p))
#ifdef FUSE_MORPHOS_DEBUG
#include <clib/debug_protos.h>
#define MOSDBG(fmt, args...) \
  KPrintF("[FuseMOS] " fmt "\n", ##args)
#else
#define MOSDBG(fmt, args...) ((void)0)
#endif
#else
#define MOSDBG(fmt, args...) ((void)0)
#define MOSPTR(p) 0UL
#endif

#endif
