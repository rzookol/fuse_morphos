/* timer.c: UNIX speed routines for Fuse
   Copyright (c) 1999-2021 Philip Kendall, Marek Januszewski, Fredrick Meunier

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

#include <stdio.h>
#include <stdlib.h>

#ifdef __MORPHOS__
#include <devices/timer.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <proto/exec.h>
#else
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "compat.h"
#include "ui/ui.h"

#ifdef __MORPHOS__
/*
 * Do not use libnix usleep() for Fuse pacing on MorphOS.  The working
 * openMSX MorphOS port deliberately uses timer.device/UNIT_MICROHZ for its
 * main-task sleeps.  Keep one synchronous timer request for both the clock
 * samples and 10 ms pacing sleeps used by timer/timer.c.
 */
static struct MsgPort *morphos_timer_port;
static struct timerequest *morphos_timer_request;
static int morphos_timer_state; /* 0 = unopened, 1 = ready, -1 = failed */
static double morphos_timer_last_time;

static void
morphos_timer_cleanup( void )
{
  if( morphos_timer_request ) {
    if( morphos_timer_state == 1 )
      CloseDevice( (struct IORequest*)morphos_timer_request );
    DeleteIORequest( (struct IORequest*)morphos_timer_request );
    morphos_timer_request = NULL;
  }
  if( morphos_timer_port ) {
    DeleteMsgPort( morphos_timer_port );
    morphos_timer_port = NULL;
  }
  morphos_timer_state = 0;
}

static int
morphos_timer_init( void )
{
  if( morphos_timer_state ) return morphos_timer_state > 0;

  morphos_timer_port = CreateMsgPort();
  if( !morphos_timer_port ) goto fail;
  morphos_timer_request = (struct timerequest*)
    CreateIORequest( morphos_timer_port, sizeof( *morphos_timer_request ) );
  if( !morphos_timer_request ) goto fail;
  if( OpenDevice( TIMERNAME, UNIT_MICROHZ,
                  (struct IORequest*)morphos_timer_request, 0 ) != 0 )
    goto fail;

  morphos_timer_state = 1;
  atexit( morphos_timer_cleanup );
  return 1;

fail:
  fprintf( stderr, "Fuse: MorphOS timer.device initialization failed\n" );
  /* CloseDevice is valid only after a successful OpenDevice(). */
  if( morphos_timer_request ) {
    DeleteIORequest( (struct IORequest*)morphos_timer_request );
    morphos_timer_request = NULL;
  }
  if( morphos_timer_port ) {
    DeleteMsgPort( morphos_timer_port );
    morphos_timer_port = NULL;
  }
  morphos_timer_state = -1;
  return 0;
}
#endif

double
compat_timer_get_time( void )
{
#ifdef __MORPHOS__
  double now;
  struct timerequest *tr;

  if( !morphos_timer_init() ) return -1;
  tr = morphos_timer_request;
  tr->tr_node.io_Command = TR_GETSYSTIME;
  tr->tr_node.io_Error = 0;
  if( DoIO( (struct IORequest*)tr ) != 0 ) return -1;

  now = (double)tr->tr_time.tv_secs +
        (double)tr->tr_time.tv_micro / 1000000.0;

  /* Wall-clock adjustments must never make the emulation clock go backwards. */
  if( now < morphos_timer_last_time ) now = morphos_timer_last_time;
  morphos_timer_last_time = now;
  return now;
#else
  struct timeval tv;
  int error;

  error = gettimeofday( &tv, NULL );
  if( error ) {
    ui_error( UI_ERROR_ERROR, "%s: error getting time: %s", __func__, strerror( errno ) );
    return -1;
  }

  return tv.tv_sec + tv.tv_usec / 1000000.0;
#endif
}

void
compat_timer_sleep( int ms )
{
#ifdef __MORPHOS__
  struct timerequest *tr;
  if( ms <= 0 || !morphos_timer_init() ) return;
  tr = morphos_timer_request;
  tr->tr_node.io_Command = TR_ADDREQUEST;
  tr->tr_node.io_Error = 0;
  tr->tr_time.tv_secs = (ULONG)( ms / 1000 );
  tr->tr_time.tv_micro = (ULONG)( ms % 1000 ) * 1000UL;
  (void)DoIO( (struct IORequest*)tr );
#else
  usleep( ms * 1000 );
#endif
}
