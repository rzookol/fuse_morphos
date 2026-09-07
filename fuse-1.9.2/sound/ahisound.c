/* ahisound.c: native MorphOS AHI sound output
   Non-blocking producer + native Exec/AHI worker, modelled after the
   MorphOS openMSX driver but implemented in plain C for Fuse. */

#include "config.h"

#ifdef __MORPHOS__

#include <devices/ahi.h>
#include <exec/io.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <utility/tagitem.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libspectrum.h"
#include "sound.h"
#include "ui/ui.h"
#include "ui/morphos/morphosdebug.h"

#define AHI_REQUEST_COUNT 2
#define PCM_SLOT_COUNT 12
#define MIN_AHI_FRAMES 2048
#define START_QUEUE_DEPTH 2
#define QUEUE_TARGET_DEPTH 2
#define RECOVERY_SILENCE_FRAMES 512
#define CLOCK_CORRECTION_PPM_PER_SLOT 3750
#define MAX_CLOCK_CORRECTION_PPM 7500
#define NO_SLOT ((ULONG)~0UL)
#define NO_ACTIVE_SLOT (-1)
#define SILENCE_ACTIVE_SLOT (-2)
#define AHI_VOLUME_FULL 0x10000
#define AHI_PAN_CENTER 0x8000

#define SLOT_FREE 0
#define SLOT_WRITING 1
#define SLOT_QUEUED 2
#define SLOT_INFLIGHT 3

typedef struct ahi_pcm_slot {
  libspectrum_signed_word *data;
  ULONG samples;
  volatile ULONG state;
} ahi_pcm_slot;

static ahi_pcm_slot pcm[PCM_SLOT_COUNT];
static ULONG queue_slots[PCM_SLOT_COUNT];
static volatile ULONG queue_write;
static volatile ULONG queue_read;
static LONG producer_slot = -1;
static ULONG producer_fill_samples;
static ULONG fragment_frames;
static ULONG fragment_samples;
static ULONG channels;
static ULONG ahi_frequency;
static libspectrum_signed_word *silence_buffer;

/* Worker-owned AHI state. */
static struct MsgPort *ahi_port;
static struct AHIRequest *ahi_request[AHI_REQUEST_COUNT];
static int ahi_pending[AHI_REQUEST_COUNT];
static int ahi_active_slot[AHI_REQUEST_COUNT] = { NO_ACTIVE_SLOT, NO_ACTIVE_SLOT };
static int ahi_started;
static int ahi_device_open;
static struct Task *ahi_worker_task;
static LONG ahi_worker_signal_bit = -1;
static ULONG ahi_worker_signal_mask;
static ULONG ahi_reply_signal_mask;

/* Main-task synchronisation. One private signal is retained for the lifetime
   of the driver and used for both init-complete and worker-exit handshakes. */
static struct Task *ahi_main_task;
static LONG ahi_main_signal_bit = -1;
static ULONG ahi_main_signal_mask;
static volatile int ahi_init_done;
static volatile int ahi_init_ok;
static volatile int ahi_stop_requested;
static volatile int ahi_reset_requested;
static volatile int ahi_init_error;

/* Exactly like the working openMSX MorphOS Worker: TASKTAG_STARTUPMSG is
   replied by Exec only after RemTask() has really removed the worker.  This
   makes sound_lowlevel_end() a true join instead of racing the last few
   instructions of the AHI task. */
static struct MsgPort ahi_join_port;
static struct Message ahi_startup_message;
static int ahi_join_ready;

static void ahi_worker_main( void );

static void
memory_barrier( void )
{
  __sync_synchronize();
}

static void
signal_main( void )
{
  memory_barrier();
  if( ahi_main_task && ahi_main_signal_mask )
    Signal( ahi_main_task, ahi_main_signal_mask );
}

static ULONG
queue_depth( void )
{
  ULONG read = queue_read;
  memory_barrier();
  return queue_write - read;
}

static ULONG
producer_find_free_slot( void )
{
  ULONG i;
  for( i = 0; i < PCM_SLOT_COUNT; ++i ) {
    if( __sync_bool_compare_and_swap( &pcm[i].state,
                                     SLOT_FREE, SLOT_WRITING ) )
      return i;
  }
  return NO_SLOT;
}

static void
producer_discard_partial( void )
{
  if( producer_slot < 0 ) return;
  pcm[producer_slot].state = SLOT_FREE;
  memory_barrier();
  producer_slot = -1;
  producer_fill_samples = 0;
}

static void
wake_worker( void )
{
  struct Task *task = ahi_worker_task;
  ULONG mask = ahi_worker_signal_mask;
  memory_barrier();
  if( task && mask ) Signal( task, mask );
}

static ULONG
worker_pop_queued( void )
{
  ULONG read = queue_read;
  ULONG write;
  ULONG slot;

  memory_barrier();
  write = queue_write;
  if( read == write ) return NO_SLOT;
  slot = queue_slots[read % PCM_SLOT_COUNT];
  queue_read = read + 1;
  memory_barrier();
  pcm[slot].state = SLOT_INFLIGHT;
  memory_barrier();
  return slot;
}

static void
worker_clear_queue( void )
{
  ULONG slot;
  while( ( slot = worker_pop_queued() ) != NO_SLOT ) {
    pcm[slot].state = SLOT_FREE;
  }
  producer_discard_partial();
  queue_read = queue_write;
  memory_barrier();
}

static ULONG
worker_playback_frequency( void )
{
  LONG depth = (LONG)queue_depth();
  LONG error = depth - QUEUE_TARGET_DEPTH;
  LONG ppm = error * CLOCK_CORRECTION_PPM_PER_SLOT;
  int64_t scaled;

  if( ppm > MAX_CLOCK_CORRECTION_PPM ) ppm = MAX_CLOCK_CORRECTION_PPM;
  if( ppm < -MAX_CLOCK_CORRECTION_PPM ) ppm = -MAX_CLOCK_CORRECTION_PPM;
  scaled = (int64_t)ahi_frequency * ( 1000000LL + ppm );
  scaled = ( scaled + 500000LL ) / 1000000LL;
  if( scaled < 1 ) scaled = 1;
  return (ULONG)scaled;
}

static int
worker_reap_completed( unsigned request )
{
  struct IORequest *io;
  int slot;

  if( !ahi_pending[request] ) return 0;
  io = (struct IORequest*)ahi_request[request];
  if( !CheckIO( io ) ) return 0;
  WaitIO( io );
  ahi_pending[request] = 0;

  slot = ahi_active_slot[request];
  if( slot >= 0 ) {
    pcm[slot].state = SLOT_FREE;
    memory_barrier();
  }
  ahi_active_slot[request] = NO_ACTIVE_SLOT;
  return 1;
}

static void
worker_submit( unsigned request, ULONG slot, int previous_request )
{
  struct AHIRequest *r = ahi_request[request];
  ahi_pcm_slot *src = &pcm[slot];

  r->ahir_Std.io_Command = CMD_WRITE;
  r->ahir_Std.io_Data = src->data;
  r->ahir_Std.io_Length = src->samples * sizeof( libspectrum_signed_word );
  r->ahir_Std.io_Offset = 0;
  r->ahir_Std.io_Error = 0;
  r->ahir_Std.io_Actual = 0;
  r->ahir_Frequency = worker_playback_frequency();
  r->ahir_Type = channels == 2 ? AHIST_S16S : AHIST_M16S;
  r->ahir_Volume = AHI_VOLUME_FULL;
  r->ahir_Position = AHI_PAN_CENTER;
  r->ahir_Link = previous_request >= 0 ? ahi_request[previous_request] : NULL;

  SendIO( (struct IORequest*)r );
  ahi_pending[request] = 1;
  ahi_active_slot[request] = (int)slot;
}

static void
worker_submit_silence( unsigned request, int previous_request )
{
  struct AHIRequest *r = ahi_request[request];
  ULONG frames = fragment_frames < RECOVERY_SILENCE_FRAMES ?
                 fragment_frames : RECOVERY_SILENCE_FRAMES;
  ULONG samples = frames * channels;

  r->ahir_Std.io_Command = CMD_WRITE;
  r->ahir_Std.io_Data = silence_buffer;
  r->ahir_Std.io_Length = samples * sizeof( libspectrum_signed_word );
  r->ahir_Std.io_Offset = 0;
  r->ahir_Std.io_Error = 0;
  r->ahir_Std.io_Actual = 0;
  r->ahir_Frequency = worker_playback_frequency();
  r->ahir_Type = channels == 2 ? AHIST_S16S : AHIST_M16S;
  r->ahir_Volume = AHI_VOLUME_FULL;
  r->ahir_Position = AHI_PAN_CENTER;
  r->ahir_Link = previous_request >= 0 ? ahi_request[previous_request] : NULL;

  SendIO( (struct IORequest*)r );
  ahi_pending[request] = 1;
  ahi_active_slot[request] = SILENCE_ACTIVE_SLOT;
}

static void
worker_start_pair( int initial_prime )
{
  ULONG first, second;
  if( ahi_pending[0] || ahi_pending[1] ) return;
  if( initial_prime && queue_depth() < START_QUEUE_DEPTH ) return;

  first = worker_pop_queued();
  second = worker_pop_queued();
  if( first != NO_SLOT ) worker_submit( 0, first, -1 );
  else worker_submit_silence( 0, -1 );
  if( second != NO_SLOT ) worker_submit( 1, second, 0 );
  else worker_submit_silence( 1, 0 );
  ahi_started = 1;
}

static void
worker_service( void )
{
  unsigned pass;
  worker_reap_completed( 0 );
  worker_reap_completed( 1 );

  if( !ahi_started ) {
    worker_start_pair( 1 );
    return;
  }
  if( !ahi_pending[0] && !ahi_pending[1] ) {
    ahi_started = 0;
    worker_start_pair( 0 );
    return;
  }

  for( pass = 0; pass < AHI_REQUEST_COUNT; ++pass ) {
    int free_request = !ahi_pending[0] ? 0 : ( !ahi_pending[1] ? 1 : -1 );
    int other;
    ULONG slot;
    if( free_request < 0 ) break;
    other = free_request ^ 1;
    if( !ahi_pending[other] ) break;
    slot = worker_pop_queued();
    if( slot != NO_SLOT ) worker_submit( (unsigned)free_request, slot, other );
    else worker_submit_silence( (unsigned)free_request, other );
  }
}

static void
worker_stop_playback( void )
{
  unsigned i;
  for( i = 0; i < AHI_REQUEST_COUNT; ++i ) {
    if( ahi_pending[i] ) {
      struct IORequest *io = (struct IORequest*)ahi_request[i];
      if( !CheckIO( io ) ) AbortIO( io );
    }
  }
  for( i = 0; i < AHI_REQUEST_COUNT; ++i ) {
    if( ahi_pending[i] ) {
      WaitIO( (struct IORequest*)ahi_request[i] );
      ahi_pending[i] = 0;
    }
    if( ahi_active_slot[i] >= 0 )
      pcm[ahi_active_slot[i]].state = SLOT_FREE;
    ahi_active_slot[i] = NO_ACTIVE_SLOT;
  }
  worker_clear_queue();
  ahi_started = 0;
}

static int
worker_open_ahi( void )
{
  ahi_port = CreateMsgPort();
  if( !ahi_port ) return 1;
  ahi_reply_signal_mask = 1UL << ahi_port->mp_SigBit;

  ahi_request[0] = (struct AHIRequest*)CreateIORequest(
      ahi_port, sizeof( struct AHIRequest ) );
  if( !ahi_request[0] ) return 2;
  ahi_request[0]->ahir_Version = 4;
  if( OpenDevice( AHINAME, AHI_DEFAULT_UNIT,
                  (struct IORequest*)ahi_request[0], 0 ) != 0 ) return 3;
  ahi_device_open = 1;

  ahi_request[1] = (struct AHIRequest*)AllocMem(
      sizeof( struct AHIRequest ), MEMF_ANY );
  if( !ahi_request[1] ) return 4;
  CopyMem( ahi_request[0], ahi_request[1], sizeof( struct AHIRequest ) );
  return 0;
}

static void
worker_close_ahi( void )
{
  worker_stop_playback();
  if( ahi_device_open && ahi_request[0] ) {
    CloseDevice( (struct IORequest*)ahi_request[0] );
    ahi_device_open = 0;
  }
  if( ahi_request[1] ) {
    FreeMem( ahi_request[1], sizeof( struct AHIRequest ) );
    ahi_request[1] = NULL;
  }
  if( ahi_request[0] ) {
    DeleteIORequest( (struct IORequest*)ahi_request[0] );
    ahi_request[0] = NULL;
  }
  if( ahi_port ) {
    DeleteMsgPort( ahi_port );
    ahi_port = NULL;
  }
  ahi_reply_signal_mask = 0;
}

ABOX_TASK( void, FuseAHITask, void *arg )
{
  (void)arg;
  ahi_worker_main();
  RemTask( NULL );
}

static void
ahi_worker_main( void )
{
  int error;

  ahi_worker_task = FindTask( NULL );
  ahi_worker_signal_bit = AllocSignal( -1 );
  if( ahi_worker_signal_bit >= 0 )
    ahi_worker_signal_mask = 1UL << ahi_worker_signal_bit;
  else
    ahi_worker_signal_mask = 0;

  error = ahi_worker_signal_mask ? worker_open_ahi() : 5;
  ahi_init_error = error;
  ahi_init_ok = error == 0;
  ahi_init_done = 1;
  signal_main();

  if( error == 0 ) {
    while( !ahi_stop_requested ) {
      if( ahi_reset_requested ) {
        ahi_reset_requested = 0;
        worker_stop_playback();
      }
      worker_service();
      if( ahi_stop_requested ) break;
      Wait( ahi_reply_signal_mask | ahi_worker_signal_mask );
    }
    worker_close_ahi();
  } else {
    worker_close_ahi();
  }

  if( ahi_worker_signal_bit >= 0 ) FreeSignal( ahi_worker_signal_bit );
  ahi_worker_signal_bit = -1;
  ahi_worker_signal_mask = 0;
  ahi_worker_task = NULL;
  MOSDBG( "AHI worker main returning; RemTask follows" );
}

static void
init_worker_join( void )
{
  memset( &ahi_join_port, 0, sizeof( ahi_join_port ) );
  ahi_join_port.mp_Node.ln_Type = NT_MSGPORT;
  ahi_join_port.mp_Flags = PA_SIGNAL;
  ahi_join_port.mp_SigTask = ahi_main_task;
  ahi_join_port.mp_SigBit = (UBYTE)ahi_main_signal_bit;
  NEWLIST( &ahi_join_port.mp_MsgList );

  memset( &ahi_startup_message, 0, sizeof( ahi_startup_message ) );
  ahi_startup_message.mn_Node.ln_Type = NT_MESSAGE;
  ahi_startup_message.mn_ReplyPort = &ahi_join_port;
  ahi_startup_message.mn_Length = sizeof( ahi_startup_message );
  ahi_join_ready = 1;
}

static int
join_worker( void )
{
  struct Message *msg;

  if( !ahi_join_ready ) return 1;
  msg = GetMsg( &ahi_join_port );
  while( !msg ) {
    Wait( ahi_main_signal_mask );
    msg = GetMsg( &ahi_join_port );
  }
  ahi_join_ready = 0;
  MOSDBG( "AHI worker joined msg=%08lx expected=%08lx",
          MOSPTR( msg ), MOSPTR( &ahi_startup_message ) );
  return msg == &ahi_startup_message;
}

static void
free_pcm( void )
{
  ULONG i;
  for( i = 0; i < PCM_SLOT_COUNT; ++i ) {
    if( pcm[i].data ) FreeVec( pcm[i].data );
    pcm[i].data = NULL;
    pcm[i].samples = 0;
    pcm[i].state = SLOT_FREE;
  }
  if( silence_buffer ) FreeVec( silence_buffer );
  silence_buffer = NULL;
}

static void
reset_globals( void )
{
  unsigned i;
  queue_write = queue_read = 0;
  producer_slot = -1;
  producer_fill_samples = 0;
  ahi_started = 0;
  ahi_device_open = 0;
  ahi_port = NULL;
  ahi_request[0] = ahi_request[1] = NULL;
  for( i = 0; i < AHI_REQUEST_COUNT; ++i ) {
    ahi_pending[i] = 0;
    ahi_active_slot[i] = NO_ACTIVE_SLOT;
  }
  ahi_worker_task = NULL;
  ahi_worker_signal_bit = -1;
  ahi_worker_signal_mask = 0;
  ahi_reply_signal_mask = 0;
  ahi_init_done = ahi_init_ok = 0;
  ahi_stop_requested = ahi_reset_requested = 0;
  ahi_join_ready = 0;
  ahi_init_error = 0;
}

int
sound_lowlevel_init( const char *device, int *freqptr, int *stereoptr )
{
  ULONG i;
  ULONG error = TASKERROR_OK;
  struct TagItem tags[9];
  struct Task *task;

  (void)device;
  reset_globals();
  ahi_frequency = ( *freqptr > 0 ) ? (ULONG)*freqptr : 44100;
  channels = *stereoptr ? 2 : 1;
  fragment_frames = MIN_AHI_FRAMES;
  fragment_samples = fragment_frames * channels;

  for( i = 0; i < PCM_SLOT_COUNT; ++i ) {
    pcm[i].data = (libspectrum_signed_word*)AllocVec(
        fragment_samples * sizeof( libspectrum_signed_word ), MEMF_ANY );
    if( !pcm[i].data ) {
      ui_error( UI_ERROR_ERROR, "AHI: unable to allocate PCM ring" );
      free_pcm();
      return 1;
    }
    pcm[i].state = SLOT_FREE;
  }
  silence_buffer = (libspectrum_signed_word*)AllocVec(
      fragment_samples * sizeof( libspectrum_signed_word ), MEMF_ANY | MEMF_CLEAR );
  if( !silence_buffer ) {
    ui_error( UI_ERROR_ERROR, "AHI: unable to allocate silence buffer" );
    free_pcm();
    return 1;
  }

  ahi_main_task = FindTask( NULL );
  ahi_main_signal_bit = AllocSignal( -1 );
  if( ahi_main_signal_bit < 0 ) {
    ui_error( UI_ERROR_ERROR, "AHI: unable to allocate synchronisation signal" );
    free_pcm();
    return 1;
  }
  ahi_main_signal_mask = 1UL << ahi_main_signal_bit;
  init_worker_join();

  MOSDBG( "AHI init freq=%lu channels=%lu fragment_frames=%lu",
          ahi_frequency, channels, fragment_frames );

  tags[0].ti_Tag = TASKTAG_ERROR; tags[0].ti_Data = (ULONG)(uintptr_t)&error;
  tags[1].ti_Tag = TASKTAG_CODETYPE; tags[1].ti_Data = CODETYPE_PPC;
  tags[2].ti_Tag = TASKTAG_PC; tags[2].ti_Data = (ULONG)(uintptr_t)ABOX_TASKREF( FuseAHITask );
  tags[3].ti_Tag = TASKTAG_STACKSIZE; tags[3].ti_Data = 65536;
  tags[4].ti_Tag = TASKTAG_NAME; tags[4].ti_Data = (ULONG)(uintptr_t)"Fuse AHI";
  tags[5].ti_Tag = TASKTAG_PRI; tags[5].ti_Data = 5;
  tags[6].ti_Tag = TASKTAG_PPC_ARG1; tags[6].ti_Data = 0;
  tags[7].ti_Tag = TASKTAG_STARTUPMSG;
  tags[7].ti_Data = (ULONG)(uintptr_t)&ahi_startup_message;
  tags[8].ti_Tag = TAG_DONE; tags[8].ti_Data = 0;

  task = NewCreateTaskA( tags );
  if( !task ) {
    ui_error( UI_ERROR_ERROR, error == TASKERROR_NOMEMORY ?
              "AHI: unable to create worker task (out of memory)" :
              "AHI: unable to create worker task" );
    FreeSignal( ahi_main_signal_bit );
    ahi_main_signal_bit = -1;
    ahi_main_signal_mask = 0;
    ahi_join_ready = 0;
    free_pcm();
    return 1;
  }

  while( !ahi_init_done ) Wait( ahi_main_signal_mask );
  memory_barrier();
  if( !ahi_init_ok ) {
    (void)join_worker();
    switch( ahi_init_error ) {
    case 1: ui_error( UI_ERROR_ERROR, "AHI: CreateMsgPort failed" ); break;
    case 2: ui_error( UI_ERROR_ERROR, "AHI: CreateIORequest failed" ); break;
    case 3: ui_error( UI_ERROR_ERROR, "AHI: unable to open ahi.device default unit" ); break;
    case 4: ui_error( UI_ERROR_ERROR, "AHI: unable to allocate second request" ); break;
    default: ui_error( UI_ERROR_ERROR, "AHI: worker initialisation failed" ); break;
    }
    FreeSignal( ahi_main_signal_bit );
    ahi_main_signal_bit = -1;
    ahi_main_signal_mask = 0;
    ahi_main_task = NULL;
    free_pcm();
    return 1;
  }

  *freqptr = (int)ahi_frequency;
  return 0;
}

void
sound_lowlevel_end( void )
{
  MOSDBG( "AHI end begin worker=%08lx join_ready=%ld",
          MOSPTR( ahi_worker_task ), (LONG)ahi_join_ready );
  if( ahi_worker_task || ahi_join_ready ) {
    producer_discard_partial();
    ahi_stop_requested = 1;
    memory_barrier();
    wake_worker();
    (void)join_worker();
  }

  if( ahi_main_signal_bit >= 0 ) FreeSignal( ahi_main_signal_bit );
  ahi_main_signal_bit = -1;
  ahi_main_signal_mask = 0;
  ahi_main_task = NULL;
  free_pcm();
  reset_globals();
  MOSDBG( "AHI end done" );
}

void
sound_lowlevel_frame( libspectrum_signed_word *data, int len )
{
  ULONG source = 0;
  ULONG total;
  int queued_any = 0;

  if( !ahi_init_ok || !data || len <= 0 ) return;
  total = (ULONG)len;

  while( source < total ) {
    ahi_pcm_slot *dst;
    ULONG room, count, write, read;

    if( producer_slot < 0 ) {
      ULONG slot = producer_find_free_slot();
      if( slot == NO_SLOT ) break; /* Audio must never throttle emulation. */
      producer_slot = (LONG)slot;
      producer_fill_samples = 0;
    }

    dst = &pcm[producer_slot];
    room = fragment_samples - producer_fill_samples;
    count = total - source;
    if( count > room ) count = room;
    CopyMem( data + source, dst->data + producer_fill_samples,
             count * sizeof( libspectrum_signed_word ) );
    producer_fill_samples += count;
    source += count;

    if( producer_fill_samples != fragment_samples ) continue;

    write = queue_write;
    memory_barrier();
    read = queue_read;
    if( write - read >= PCM_SLOT_COUNT ) {
      dst->state = SLOT_FREE;
      producer_slot = -1;
      producer_fill_samples = 0;
      break;
    }

    dst->samples = fragment_samples;
    queue_slots[write % PCM_SLOT_COUNT] = (ULONG)producer_slot;
    dst->state = SLOT_QUEUED;
    memory_barrier();
    queue_write = write + 1;
    producer_slot = -1;
    producer_fill_samples = 0;
    queued_any = 1;
  }

  if( queued_any ) wake_worker();
}

#endif /* __MORPHOS__ */
