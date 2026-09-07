/* morphosui.c: native MorphOS/MUI frontend for Fuse
   Initial native backend derived from the architecture used by the
   openMSX MorphOS port, adapted to Fuse's ui/uidisplay interfaces. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MORPHOS__
#include <exec/libraries.h>
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <utility/hooks.h>
#include <mui/Aboutbox_mcc.h>
#include <devices/rawkeycodes.h>
#include <emul/emulinterface.h>
#include <emul/emulregs.h>
#include <proto/alib.h>
#include <proto/cybergraphics.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#ifndef MAKE_ID
#define MAKE_ID(a,b,c,d) (((ULONG)(a) << 24) | ((ULONG)(b) << 16) | \
                          ((ULONG)(c) << 8) | (ULONG)(d))
#endif
#endif

#include "debugger/debugger.h"
#include "display.h"
#include "memory_pages.h"
#include "pokefinder/pokefinder.h"
#include "machine.h"
#include "machines/specplus3.h"
#include "tape.h"
#include "fuse.h"
#include "menu.h"
#include "settings.h"
#include "timer/timer.h"
#include "ui/ui.h"
#include "ui/uidisplay.h"
#include "ui/scaler/scaler.h"
#include "utils.h"
#include "morphoskeyboard.h"
#include "locale.h"
#include "morphosjoystick.h"
#include "morphosvideo.h"
#include "morphosdisplay.h"
#include "morphosdebug.h"
#include "morphosui.h"
#include "z80/z80.h"
#include "z80/z80_macros.h"

#ifdef __MORPHOS__
struct GfxBase *GfxBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library *CyberGfxBase = NULL;
struct Library *MUIMasterBase = NULL;
#endif

#define MORPHOS_APP_TITLE       "Fuse"
#define MORPHOS_APP_COPYRIGHT   "1999-2026 Philip Kendall and others; 2026 Michal Zukowski"
#define MORPHOS_APP_AUTHOR      "Michal Zukowski"
#define MORPHOS_APP_VERSION     "$VER: Fuse " VERSION " (05.09.2026)"
#define MORPHOS_APP_BASE        "FUSE"
#define MORPHOS_APP_DESCRIPTION "ZX Spectrum emulator"

#define RID_QUIT              0x7100
#define RID_APPDROP           0x7101
#define RID_AREXX_RESET       0x7102
#define RID_AREXX_HARDRESET   0x7103
#define RID_AREXX_PAUSE       0x7104
#define RID_AREXX_FULLSCREEN  0x7105
#define RID_MENU_BASE         0x7200
#define RID_FILTER_APPLY      0x7e10
#define RID_FILTER_DEFAULTS   0x7e11
#define RID_FILTER_CLOSE      0x7e12
#define RID_FILTER_LIVE_BASE  0x7e60
#define RID_TAPE_SELECT       0x7e20
#define RID_TAPE_CLOSE        0x7e21
#define RID_MEM_PREV          0x7e30
#define RID_MEM_NEXT          0x7e31
#define RID_MEM_GOTO          0x7e32
#define RID_MEM_CLOSE         0x7e33
#define RID_POKE_INC          0x7e40
#define RID_POKE_DEC          0x7e41
#define RID_POKE_SEARCH       0x7e42
#define RID_POKE_RESET        0x7e43
#define RID_POKE_BREAK        0x7e44
#define RID_POKE_CLOSE        0x7e45
#define RID_DEBUG_REFRESH     0x7e50
#define RID_DEBUG_COMMAND     0x7e51
#define RID_DEBUG_CLOSE       0x7e52
#define MAX_BINDINGS   384
#define MAX_MENU_OBJECTS 512
#define MAX_MACHINE_MENU 64
#define MORPHOS_TAPE_MAX_BLOCKS 1024
#define MORPHOS_POKE_MAX_POSSIBLE 20

typedef void (*morphos_menu_cb)( int action );
typedef struct morphos_menu_binding {
  morphos_menu_cb callback;
  int action;
} morphos_menu_binding;

typedef struct morphos_menu_object {
  void *object;
  int is_menu;
  char path[160];
} morphos_menu_object;

static void *app;
static void *win;
static void *fullscreen_win;
#ifdef __MORPHOS__
static struct Window *native_window;
static struct Screen *fullscreen_screen;
static unsigned fullscreen_screen_width;
static unsigned fullscreen_screen_height;
static int fullscreen_screen_surface;
#endif
static void *area;
static void *window_area;
static void *fullscreen_area;
static void *area_class;
static unsigned logical_width = 320;
static unsigned logical_height = 240;
static const libspectrum_word *last_pixels;
static unsigned last_width, last_height, last_pitch;
static morphos_menu_binding bindings[MAX_BINDINGS];
static unsigned binding_count;
static morphos_menu_object menu_objects[MAX_MENU_OBJECTS];
static unsigned menu_object_count;
static int window_open;
static int mouse_valid;
static int last_mouse_x, last_mouse_y;
static int mouse_grabbed_native;
#ifdef __MORPHOS__
static ULONG last_video_click_seconds;
static ULONG last_video_click_micros;
static int last_video_click_valid;
#endif
static int widget_initialised;
static char window_title[128] = "Fuse";
static int video_attached;
static int ui_shutting_down;
static void *video_surface_item;
static void *video_overlay_item;
static void *video_tinygl_item;
static void *video_vsync_item;
static void *video_linear_item;
static void *video_border_item;
static void *fullscreen_item;
static void *scaler_menu_item[SCALER_NUM];
static void *machine_select_menu;
static void *machine_menu_item[MAX_MACHINE_MENU];
static libspectrum_machine machine_menu_type[MAX_MACHINE_MENU];
static int machine_menu_count;
static void *input_joy_port_item[2][4];
static void *input_grab_mouse_item;
static void *control_group;
static void *control_reset;
static void *control_pause;
static void *control_fast;
static void *control_tape_play;
static void *control_tape_stop;
static void *control_tape_rewind;
static void *control_disk_a_eject;
static void *control_disk_b_eject;
static void *status_group;
static void *status_machine;
static void *status_speed;
static void *status_tape;
static void *status_disk;
static void *status_mdr;
static void *status_mouse;
static void *status_pause;
static int fullscreen_native;
static int paused_native;
static long windowed_left, windowed_top, windowed_width, windowed_height;
static int requester_depth;
static int requester_overlay_switched;
static morphos_video_mode requester_restore_mode;
static int fast_speed_saved = 100;
static libspectrum_machine status_machine_type = LIBSPECTRUM_MACHINE_UNKNOWN;
static char requester_drawer[1024] = "PROGDIR:";
static void *poke_window;
static void *poke_list;
static void *poke_count_text;
static void *poke_entry;
static int poke_pages[MORPHOS_POKE_MAX_POSSIBLE];
static libspectrum_word poke_offsets[MORPHOS_POKE_MAX_POSSIBLE];
static size_t poke_visible;
static int poke_overlay_guard;
static int debugger_close_requested;
static libspectrum_word debugger_disassembly_address;
static void *debugger_window;
static void *debugger_text;
static void *debugger_command;

#ifdef __MORPHOS__
static unsigned morphos_draw_trace_count;
static unsigned morphos_event_trace_count;
#endif

static int draw_video( void );
static int draw_video_real( void );
#ifdef __MORPHOS__
/* Cache the native Intuition Window only from the Area lifecycle.
   Calling GetAttr(MUIA_Window_Window) while MUIM_Show/MUIM_Draw is already
   executing can re-enter MUI while the same window is being opened.  The
   Area helper is valid in MUIM_Show, and MUIM_Hide clears the cache before
   the native window can disappear. */
static struct Window *
morphosui_native_window( void )
{
  /* Same lifecycle contract as openMSX MorphOS: cache _window(obj) in
     MUIM_Show and clear it in MUIM_Hide. Do not reject a MUI-owned native
     Window with TypeOfMem(); the presenter validates the fields it needs. */
  return native_window;
}

static morphos_video_mode
morphosui_fullscreen_video_mode( void )
{
  morphos_video_mode mode = morphosvideo_requested_mode();
  if( mode == MORPHOS_VIDEO_TINYGL && !morphosvideo_has_tinygl() )
    mode = morphosvideo_has_overlay() ? MORPHOS_VIDEO_OVERLAY
                                      : MORPHOS_VIDEO_SURFACE;
  if( mode == MORPHOS_VIDEO_OVERLAY && !morphosvideo_has_overlay() )
    mode = MORPHOS_VIDEO_SURFACE;
  return mode;
}

static void
morphosui_close_fullscreen_screen( void )
{
  if( fullscreen_screen ) {
    MOSDBG( "fullscreen CloseScreen screen=%08lx %lux%lu",
            MOSPTR( fullscreen_screen ),
            (ULONG)fullscreen_screen_width, (ULONG)fullscreen_screen_height );
    CloseScreen( fullscreen_screen );
    fullscreen_screen = NULL;
  }
  fullscreen_screen_width = fullscreen_screen_height = 0;
  fullscreen_screen_surface = 0;
}

static int
morphosui_open_fullscreen_screen( void )
{
  struct Screen *ambient;
  STRPTR ambient_name = (STRPTR)"Ambient";
  ULONG ambient_mode, mode, depth;
  unsigned width, height;
  int surface;

  if( fullscreen_screen ) return 1;

  /* The default public screen on MorphOS is Ambient. Lock it while copying
     the mode parameters, even if the Fuse window was moved to another MUI
     screen. */
  ambient = LockPubScreen( ambient_name );
  if( !ambient ) {
    ambient_name = NULL;
    ambient = LockPubScreen( NULL );
  }
  if( !ambient || !ambient->RastPort.BitMap ) {
    if( ambient ) UnlockPubScreen( ambient_name, ambient );
    MOSDBG( "fullscreen cannot lock Ambient/default public screen" );
    return 0;
  }

  ambient_mode = GetVPModeID( &ambient->ViewPort );
  depth = GetBitMapAttr( ambient->RastPort.BitMap, BMA_DEPTH );
  if( ambient_mode == INVALID_ID || !depth ) {
    UnlockPubScreen( ambient_name, ambient );
    MOSDBG( "fullscreen invalid Ambient mode=%08lx depth=%lu",
            ambient_mode, depth );
    return 0;
  }

  surface = morphosui_fullscreen_video_mode() == MORPHOS_VIDEO_SURFACE;
  if( surface ) {
    ULONG desired_width, desired_height;
    width = logical_width ? logical_width : 320;
    height = logical_height ? logical_height : 240;
    desired_width = width;
    desired_height = height;

    /* Surface is software-scaled before presentation, so request a display
       mode whose nominal/desired size follows the currently selected Fuse
       scaler/filter output. Keep it on the same monitor as Ambient. */
    mode = BestModeID(
      BIDTAG_NominalWidth, desired_width,
      BIDTAG_NominalHeight, desired_height,
      BIDTAG_DesiredWidth, desired_width,
      BIDTAG_DesiredHeight, desired_height,
      BIDTAG_Depth, (UBYTE)depth,
      BIDTAG_MonitorID, ambient_mode & MONITOR_ID_MASK,
      TAG_DONE );
    if( mode == INVALID_ID ) {
      MOSDBG( "fullscreen Surface BestModeID %lux%lu failed; use Ambient mode %08lx",
              (ULONG)width, (ULONG)height, ambient_mode );
      mode = ambient_mode;
    }
  } else {
    /* Overlay/TinyGL clone Ambient's screen mode and dimensions exactly.
       The borderless MUI window fills that Screen; the video backend keeps
       the emulated picture aspect ratio inside the full-size Area. */
    mode = ambient_mode;
    width = ambient->Width;
    height = ambient->Height;
  }
  UnlockPubScreen( ambient_name, ambient );

  fullscreen_screen = OpenScreenTags( NULL,
      SA_DisplayID, mode,
      SA_Width, width,
      SA_Height, height,
      SA_Depth, depth,
      SA_Type, CUSTOMSCREEN,
      SA_Quiet, TRUE,
      SA_ShowTitle, FALSE,
      SA_Draggable, FALSE,
      SA_Title, (IPTR)"Fuse",
      TAG_DONE );
  if( !fullscreen_screen ) {
    MOSDBG( "fullscreen OpenScreenTags FAILED mode=%08lx request=%lux%lu depth=%lu surface=%ld",
            mode, (ULONG)width, (ULONG)height, depth, (LONG)surface );
    return 0;
  }

  fullscreen_screen_width = width;
  fullscreen_screen_height = height;
  fullscreen_screen_surface = surface;
  MOSDBG( "fullscreen OpenScreenTags OK screen=%08lx mode=%08lx request=%lux%lu actual=%lux%lu depth=%lu surface=%ld",
          MOSPTR( fullscreen_screen ), mode,
          (ULONG)width, (ULONG)height,
          (ULONG)fullscreen_screen->Width, (ULONG)fullscreen_screen->Height,
          depth, (LONG)surface );
  return 1;
}

static int
morphosui_fullscreen_screen_needs_rebuild( void )
{
  int surface = morphosui_fullscreen_video_mode() == MORPHOS_VIDEO_SURFACE;
  if( !fullscreen_screen ) return 1;
  if( surface != fullscreen_screen_surface ) return 1;
  if( surface &&
      ( fullscreen_screen_width != ( logical_width ? logical_width : 320 ) ||
        fullscreen_screen_height != ( logical_height ? logical_height : 240 ) ) )
    return 1;
  return 0;
}

static void toggle_fullscreen( int action );
static int handle_poke_return_id( ULONG id );
static void close_native_pokefinder( void );
static void refresh_native_pokefinder( void );
static void update_video_option_checks( void );
static void update_window_resize_policy( void );
static void save_video_preferences( int persistent );
static void update_machine_menu_checks( void );
static void select_machine_menu( int action );
static int morphosui_rebuild_fullscreen_screen( void );
#endif

#ifdef __MORPHOS__
static int
morphos_open_path( const char *path )
{
  int error;
  if( !path || !*path ) return 1;
  fuse_emulation_pause();
  error = utils_open_file( path, settings_current.auto_load, NULL );
  display_refresh_all();
  fuse_emulation_unpause();
  return error;
}

static void
morphos_handle_app_drop( void )
{
  ULONG value = 0;
  struct AppMessage *msg;
  char path[1024];
  LONG i;

  if( !win ) return;
  GetAttr( MUIA_AppMessage, (Object*)win, &value );
  msg = (struct AppMessage*)value;
  if( !msg || !msg->am_ArgList || !msg->am_NumArgs ) return;

  /* GTK Fuse accepts the first URI dropped on the window.  Do the same for
     Workbench/MUI AppWindow drops so archives, snapshots, tapes and disks all
     go through Fuse's normal file auto-detection. */
  for( i = 0; i < msg->am_NumArgs; ++i ) {
    struct WBArg *arg = &msg->am_ArgList[i];
    path[0] = 0;
    if( arg->wa_Lock ) {
      if( !NameFromLock( arg->wa_Lock, path, sizeof( path ) ) ) continue;
      if( arg->wa_Name && *arg->wa_Name ) {
        if( !AddPart( path, (const char*)arg->wa_Name, sizeof( path ) ) ) continue;
      }
    } else if( arg->wa_Name && *arg->wa_Name ) {
      snprintf( path, sizeof( path ), "%s", arg->wa_Name );
    }
    if( path[0] ) { morphos_open_path( path ); break; }
  }
}

#define FUSE_MUI_HOOK(name, argA2, argA1) \
  static LONG name##_GATE(void); \
  static LONG name##_GATE2(struct Hook* h, argA2 appObject, argA1 hookData); \
  static struct EmulLibEntry name##_gate = { TRAP_LIB, 0, (void (*)(void))name##_GATE }; \
  static struct Hook name##_hook = { {NULL, NULL}, (HOOKFUNC)&name##_gate, NULL, NULL }; \
  static LONG name##_GATE(void) { \
    return name##_GATE2((struct Hook*)REG_A0, (argA2)REG_A2, (argA1)REG_A1); \
  } \
  static LONG name##_GATE2(struct Hook* h, argA2 appObject, argA1 hookData)

FUSE_MUI_HOOK( fuse_arexx_open, APTR, APTR )
{
  IPTR *params = (IPTR*)hookData;
  const char *path = params ? (const char*)params[0] : NULL;
  (void)h;
  if( !path || !*path ) {
    SetAttrs( (Object*)appObject, MUIA_Application_RexxString,
              (IPTR)"OPEN requires FILE", TAG_DONE );
    return 5;
  }
  if( morphos_open_path( path ) ) {
    SetAttrs( (Object*)appObject, MUIA_Application_RexxString,
              (IPTR)"Unable to open file", TAG_DONE );
    return 10;
  }
  SetAttrs( (Object*)appObject, MUIA_Application_RexxString, (IPTR)"OK", TAG_DONE );
  return 0;
}

FUSE_MUI_HOOK( fuse_arexx_status, APTR, APTR )
{
  static char reply[256];
  const char *machine_name = machine_current ?
    libspectrum_machine_name( machine_current->machine ) : "(none)";
  const char *video = morphosvideo_active_mode() == MORPHOS_VIDEO_TINYGL ? "TinyGL" :
                      morphosvideo_active_mode() == MORPHOS_VIDEO_OVERLAY ? "Overlay" : "Surface";
  (void)h; (void)hookData;
  snprintf( reply, sizeof( reply ),
            "machine=%s speed=%d paused=%d fullscreen=%d video=%s",
            machine_name, settings_current.emulation_speed, paused_native,
            settings_current.full_screen ? 1 : 0, video );
  SetAttrs( (Object*)appObject, MUIA_Application_RexxString, (IPTR)reply, TAG_DONE );
  return 0;
}

static struct MUI_Command fuse_arexx_commands[] = {
  { (char*)"QUIT",       (char*)MC_TEMPLATE_ID, (LONG)RID_QUIT, NULL, { 0, 0, 0, 0, 0 } },
  { (char*)"RESET",      (char*)MC_TEMPLATE_ID, (LONG)RID_AREXX_RESET, NULL, { 0, 0, 0, 0, 0 } },
  { (char*)"HARDRESET",  (char*)MC_TEMPLATE_ID, (LONG)RID_AREXX_HARDRESET, NULL, { 0, 0, 0, 0, 0 } },
  { (char*)"PAUSE",      (char*)MC_TEMPLATE_ID, (LONG)RID_AREXX_PAUSE, NULL, { 0, 0, 0, 0, 0 } },
  { (char*)"FULLSCREEN", (char*)MC_TEMPLATE_ID, (LONG)RID_AREXX_FULLSCREEN, NULL, { 0, 0, 0, 0, 0 } },
  { (char*)"OPEN",       (char*)"FILE/F/A", 1, &fuse_arexx_open_hook, { 0, 0, 0, 0, 0 } },
  { (char*)"STATUS",     (char*)"", 0, &fuse_arexx_status_hook, { 0, 0, 0, 0, 0 } },
  { NULL, NULL, 0, NULL, { 0, 0, 0, 0, 0 } }
};

#undef FUSE_MUI_HOOK

/* Native MorphOS Quit must not enter Fuse's widget confirmation dialog.
   MUI already delivered an explicit Quit action, so terminate the emulation
   loop directly and let the normal fuse_end() teardown run. */
static void
morphos_request_quit( int action )
{
  (void)action;
  if( fuse_exiting ) return;
  MOSDBG( "direct Quit requested" );
  fuse_exiting = 1;
}

static int
handle_fixed_return_id( ULONG id )
{
  switch( id ) {
  case RID_QUIT:
    morphos_request_quit( 0 );
    return 1;
  case RID_APPDROP: morphos_handle_app_drop(); return 1;
  case RID_AREXX_RESET: menu_machine_reset( 0 ); return 1;
  case RID_AREXX_HARDRESET: menu_machine_reset( 1 ); return 1;
  case RID_AREXX_PAUSE: menu_machine_pause( 0 ); return 1;
  case RID_AREXX_FULLSCREEN: toggle_fullscreen( 0 ); return 1;
  default: return 0;
  }
}

typedef struct AreaData {
  struct MUI_EventHandlerNode ehnode;
  ULONG added;
} AreaData;
#endif

#ifdef __MORPHOS__
static void
register_menu_path( Object *object, const char *path, int is_menu )
{
  morphos_menu_object *entry;
  if( !object || !path || !*path || menu_object_count >= MAX_MENU_OBJECTS ) return;
  entry = &menu_objects[menu_object_count++];
  entry->object = object;
  entry->is_menu = is_menu;
  snprintf( entry->path, sizeof( entry->path ), "%s", path );
}

static Object *
add_menu_node_path( Object *parent, const char *title, const char *path )
{
  int nested = 0;
  Object *item;

  if( path && path[0] == '/' ) {
    const char *p = path + 1;
    while( *p ) {
      if( *p++ == '/' ) { nested = 1; break; }
    }
  }

  /* Menustrip children are Menu.mui objects.  Every deeper submenu is a
     Menuitem.mui which owns child Menuitems.  This mirrors MUI's hierarchy
     (and the MorphOS openMSX frontend); using Menu.mui for nested nodes can
     produce malformed or missing submenus on some MUI versions. */
  if( !nested ) {
    item = MUI_NewObject( MUIC_Menu,
                          MUIA_Menu_Title, (IPTR)title,
                          TAG_DONE );
    register_menu_path( item, path, 1 );
  } else {
    item = MUI_NewObject( MUIC_Menuitem,
                          MUIA_Menuitem_Title, (IPTR)title,
                          TAG_DONE );
    register_menu_path( item, path, 0 );
  }
  if( item && parent ) DoMethod( parent, MUIM_Family_AddTail, item );
  return item;
}

static Object *
add_separator( Object *parent )
{
  Object *item = MUI_NewObject( MUIC_Menuitem,
                                MUIA_Menuitem_Title, (IPTR)NM_BARLABEL,
                                TAG_DONE );
  if( item && parent ) DoMethod( parent, MUIM_Family_AddTail, item );
  return item;
}

static Object *
add_action_path( Object *parent, const char *title, const char *path,
                 morphos_menu_cb callback, int action )
{
  Object *item;
  ULONG rid;

  if( binding_count >= MAX_BINDINGS ) return NULL;
  item = MUI_NewObject( MUIC_Menuitem,
                        MUIA_Menuitem_Title, (IPTR)title,
                        TAG_DONE );
  if( !item ) return NULL;

  bindings[binding_count].callback = callback;
  bindings[binding_count].action = action;
  rid = RID_MENU_BASE + binding_count;
  binding_count++;

  if( parent ) DoMethod( parent, MUIM_Family_AddTail, item );
  register_menu_path( item, path, 0 );
  DoMethod( item, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, rid );
  return item;
}

static Object *
add_action( Object *parent, const char *title, morphos_menu_cb callback,
            int action )
{
  return add_action_path( parent, title, NULL, callback, action );
}


static ULONG
register_action_return_id( morphos_menu_cb callback, int action )
{
  ULONG rid;
  if( binding_count >= MAX_BINDINGS ) return 0;
  bindings[binding_count].callback = callback;
  bindings[binding_count].action = action;
  rid = RID_MENU_BASE + binding_count;
  binding_count++;
  return rid;
}

static void
notify_button( Object *button, morphos_menu_cb callback, int action )
{
  ULONG rid;
  if( !button ) return;
  rid = register_action_return_id( callback, action );
  if( !rid ) return;
  DoMethod( button, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, rid );
}

static Object *
make_image( IPTR image_spec )
{
  return ImageObject,
           MUIA_Image_Spec, image_spec,
           MUIA_Image_FreeHoriz, FALSE,
           MUIA_Image_FreeVert, FALSE,
         End;
}

static Object *
make_image_button( IPTR image_spec, const char *help, int double_image )
{
  Object *image, *image2 = NULL, *contents, *padded, *button;

  image = make_image( image_spec );
  if( !image ) return NULL;
  if( double_image ) {
    image2 = make_image( image_spec );
    if( !image2 ) { MUI_DisposeObject( image ); return NULL; }
    contents = HGroup,
                 MUIA_Group_Spacing, 0,
                 Child, (IPTR)image,
                 Child, (IPTR)image2,
               End;
    if( !contents ) {
      MUI_DisposeObject( image ); MUI_DisposeObject( image2 ); return NULL;
    }
  } else {
    contents = image;
  }

  /* Match the normal MUI text-gadget height.  The image itself is centred
     vertically, like the native openMSX MorphOS toolbar. */
  padded = VGroup,
             MUIA_Group_Spacing, 0,
             Child, (IPTR)VSpace( 0 ),
             Child, (IPTR)contents,
             Child, (IPTR)VSpace( 0 ),
           End;
  if( !padded ) { MUI_DisposeObject( contents ); return NULL; }

  button = HGroup,
             ImageButtonFrame,
             MUIA_Background, MUII_ButtonBack,
             MUIA_InputMode, MUIV_InputMode_RelVerify,
             MUIA_FixHeightTxt, (IPTR)"A",
             MUIA_CycleChain, TRUE,
             MUIA_ShortHelp, (IPTR)help,
             Child, (IPTR)HSpace( 0 ),
             Child, (IPTR)padded,
             Child, (IPTR)HSpace( 0 ),
           End;
  if( !button ) MUI_DisposeObject( padded );
  return button;
}

static Object *
make_text_button( const char *text, const char *help )
{
  return TextObject,
           ButtonFrame,
           MUIA_Background, MUII_ButtonBack,
           MUIA_InputMode, MUIV_InputMode_RelVerify,
           MUIA_Text_Contents, (IPTR)text,
           MUIA_Text_PreParse, (IPTR)"\33c",
           MUIA_FixHeightTxt, (IPTR)"A",
           MUIA_CycleChain, TRUE,
           MUIA_ShortHelp, (IPTR)help,
         End;
}

static void
update_video_menu_checks( void )
{
  morphos_video_mode mode = morphosvideo_active_mode();
  if( video_surface_item )
    SetAttrs( video_surface_item, MUIA_Menuitem_Checked,
              mode == MORPHOS_VIDEO_SURFACE, TAG_DONE );
  if( video_overlay_item )
    SetAttrs( video_overlay_item, MUIA_Menuitem_Checked,
              mode == MORPHOS_VIDEO_OVERLAY, TAG_DONE );
  if( video_tinygl_item )
    SetAttrs( video_tinygl_item, MUIA_Menuitem_Checked,
              mode == MORPHOS_VIDEO_TINYGL, TAG_DONE );
}

void
morphosui_update_scaler_menu( void )
{
#ifdef __MORPHOS__
  int i;
  for( i = 0; i < SCALER_NUM; ++i ) {
    Object *item = (Object*)scaler_menu_item[i];
    if( !item ) continue;
    SetAttrs( item,
              MUIA_Menuitem_Enabled,
                ( !scalers_registered || scaler_is_supported( (scaler_type)i ) ) ? TRUE : FALSE,
              MUIA_Menuitem_Checked, current_scaler == (scaler_type)i ? TRUE : FALSE,
              TAG_DONE );
  }
#endif
}

static void
select_scaler_menu( int action )
{
  scaler_type scaler = (scaler_type)action;
  if( scaler < 0 || scaler >= SCALER_NUM ) return;
  if( scalers_registered && !scaler_is_supported( scaler ) ) return;

  if( scaler != current_scaler ) {
    MOSDBG( "scaler menu select old=%ld new=%ld name=%s",
            (LONG)current_scaler, (LONG)scaler, scaler_name( scaler ) );
    uidisplay_set_next_hotswap_reason( UIDISPLAY_HOTSWAP_REASON_SCALER_EXPLICIT );
    if( scaler_select_scaler( scaler ) ) {
      MOSDBG( "scaler menu select FAILED scaler=%ld", (LONG)scaler );
    }
  }
  morphosui_update_scaler_menu();
  save_video_preferences( 1 );
}

void
morphosui_populate_machine_menu( void )
{
#ifdef __MORPHOS__
  int i;
  Object *sub = (Object*)machine_select_menu;

  if( !sub || machine_menu_count || machine_count <= 0 ) return;

  memset( machine_menu_item, 0, sizeof( machine_menu_item ) );
  memset( machine_menu_type, 0, sizeof( machine_menu_type ) );

  for( i = 0; i < machine_count && i < MAX_MACHINE_MENU; ++i ) {
    Object *item;
    const char *name = libspectrum_machine_name( machine_types[i]->machine );
    const char *title = name ? name : machine_types[i]->id;

    item = add_action( sub, title ? title : LOCSTR( MSG_236 ),
                       select_machine_menu, machine_menu_count );
    machine_menu_item[machine_menu_count] = item;
    machine_menu_type[machine_menu_count] = machine_types[i]->machine;
    if( item ) SetAttrs( item, MUIA_Menuitem_Checkit, TRUE, TAG_DONE );
    machine_menu_count++;
  }

  update_machine_menu_checks();
#else
  return;
#endif
}

static void
update_machine_menu_checks( void )
{
#ifdef __MORPHOS__
  int i;
  libspectrum_machine selected = LIBSPECTRUM_MACHINE_UNKNOWN;

  if( machine_current ) {
    selected = machine_current->machine;
  } else if( settings_current.start_machine ) {
    for( i = 0; i < machine_menu_count; ++i ) {
      const char *id = machine_get_id( machine_menu_type[i] );
      if( id && !strcmp( id, settings_current.start_machine ) ) {
        selected = machine_menu_type[i];
        break;
      }
    }
  }

  for( i = 0; i < machine_menu_count; ++i ) {
    Object *item = (Object*)machine_menu_item[i];
    if( !item ) continue;
    SetAttrs( item, MUIA_Menuitem_Checked,
              machine_menu_type[i] == selected ? TRUE : FALSE, TAG_DONE );
  }
#endif
}

static void
select_machine_menu( int action )
{
#ifdef __MORPHOS__
  libspectrum_machine type;

  if( action < 0 || action >= machine_menu_count ) return;
  type = machine_menu_type[action];
  if( machine_current && machine_current->machine == type ) {
    update_machine_menu_checks();
    return;
  }

  MOSDBG( "machine menu select index=%ld type=%ld name=%s",
          (LONG)action, (LONG)type, libspectrum_machine_name( type ) );
  fuse_emulation_pause();
  if( machine_select( type ) ) {
    MOSDBG( "machine menu select FAILED type=%ld", (LONG)type );
  } else {
    update_machine_menu_checks();
    save_video_preferences( 1 );
  }
  fuse_emulation_unpause();
#else
  (void)action;
#endif
}

static void
update_window_resize_policy( void )
{
#ifdef __MORPHOS__
  int resizable = morphosvideo_requested_mode() != MORPHOS_VIDEO_SURFACE;
  if( settings_current.full_screen ) resizable = 0;
  if( win )
    SetAttrs( (Object*)win, MUIA_Window_SizeGadget,
              resizable ? TRUE : FALSE, TAG_DONE );
#endif
}

static int
reopen_window_for_video_mode( morphos_video_mode mode )
{
#ifdef __MORPHOS__
  ULONG open = FALSE;
  Object *target = (Object*)( fullscreen_native ? fullscreen_win : win );
  int was_open = target && window_open;

  MOSDBG( "video switch requested=%ld current=%s was_open=%ld",
          (LONG)mode, morphosvideo_active_mode_name(), (LONG)was_open );

  if( !was_open ) {
    if( !morphosvideo_set_mode( mode ) ) return 0;
    MOSDBG( "video switch stored for next Show requested=%ld",
            (LONG)morphosvideo_requested_mode() );
    return 1;
  }

  /* Match openMSX NativeHost::setVideoMode(): close the MUI Window first.
     MUIM_Hide owns presenter detach, then set the requested mode while no
     native Window is attached and reopen so MUIM_Show gets a fresh Window. */
  MOSDBG( "video switch MUI close begin" );
  SetAttrs( target, MUIA_Window_Open, FALSE, TAG_DONE );
  window_open = 0;
  /* MUIM_Hide normally clears these. Keep a safety fallback if MUI did not
     deliver it synchronously for some reason. */
  if( video_attached || native_window ) {
    MOSDBG( "video switch close fallback detach attached=%ld native=%08lx",
            (LONG)video_attached, MOSPTR( native_window ) );
    morphosvideo_detach();
    video_attached = 0;
    native_window = NULL;
  }
  MOSDBG( "video switch MUI close done" );

  if( !morphosvideo_set_mode( mode ) ) {
    MOSDBG( "video switch set_mode FAILED mode=%ld", (LONG)mode );
    return 0;
  }
  update_window_resize_policy();

  if( fullscreen_native ) {
    /* Renderer changes can also change the private Screen policy (Surface
       uses scaler/filter size; Overlay/TinyGL clone Ambient). */
    return morphosui_rebuild_fullscreen_screen() == 0;
  }

  MOSDBG( "video switch MUI reopen begin requested=%ld",
          (LONG)morphosvideo_requested_mode() );
  SetAttrs( (Object*)win, MUIA_Window_Open, TRUE, TAG_DONE );
  GetAttr( MUIA_Window_Open, (Object*)win, &open );
  window_open = open ? 1 : 0;
  MOSDBG( "video switch MUI reopen done open=%lu native=%08lx active=%s",
          open, MOSPTR( native_window ), morphosvideo_active_mode_name() );

  if( !window_open || !morphosui_native_window() ) return 0;
  if( settings_current.full_screen ) morphosui_set_fullscreen( 1 );

  /* MUIM_Show may run before a frame is available.  If we already have the
     last Fuse framebuffer, attach/present now and report the real result. */
  draw_video();
  MOSDBG( "video switch result requested=%ld active=%s attached=%ld",
          (LONG)morphosvideo_requested_mode(), morphosvideo_active_mode_name(),
          (LONG)video_attached );
  return 1;
#else
  return morphosvideo_set_mode( mode );
#endif
}

static void
select_video_mode( int action )
{
  morphos_video_mode mode = (morphos_video_mode)action;
  MOSDBG( "video menu select mode=%ld requester_depth=%ld",
          (LONG)mode, (LONG)requester_depth );
  /* An overlay-safe dialog may have temporarily forced Surface mode.
     Changing the requested renderer in the middle of that guard would make
     dialog_end() restore stale state.  Defer renderer changes until the
     dialog/tool closes. */
  if( requester_depth > 0 ) {
    MOSDBG( "video menu select deferred: requester active" );
    return;
  }
  reopen_window_for_video_mode( mode );
  update_video_option_checks();
  save_video_preferences( 1 );
}

static Object *
add_video_action( Object *parent, const char *title, morphos_video_mode mode,
                  int enabled )
{
  Object *item = add_action( parent, title, select_video_mode, (int)mode );
  if( item ) SetAttrs( item, MUIA_Menuitem_Checkit, TRUE,
                       MUIA_Menuitem_Enabled, enabled ? TRUE : FALSE,
                       TAG_DONE );
  return item;
}

static void
load_video_preferences( void )
{
  FILE *f;
  char line[128];
  char filter[64] = "";
  char machine_id[64] = "";
  morphos_video_mode mode = MORPHOS_VIDEO_SURFACE;
  int vsync = 1, linear = 1, border = 1;

  f = fopen( "ENV:Fuse_MorphOS_prefs", "r" );
  if( !f ) f = fopen( "ENVARC:Fuse_MorphOS_prefs", "r" );
  if( f ) {
    while( fgets( line, sizeof( line ), f ) ) {
      char *eq = strchr( line, '=' );
      char *value;
      if( !eq ) continue;
      *eq++ = 0;
      value = eq;
      value[strcspn( value, "\r\n" )] = 0;
      if( !strcmp( line, "video_mode" ) ) {
        if( !strcmp( value, "gl" ) || !strcmp( value, "tinygl" ) )
          mode = MORPHOS_VIDEO_TINYGL;
        else if( !strcmp( value, "overlay" ) )
          mode = MORPHOS_VIDEO_OVERLAY;
        else mode = MORPHOS_VIDEO_SURFACE;
      } else if( !strcmp( line, "filter" ) || !strcmp( line, "scaler" ) ) {
        snprintf( filter, sizeof( filter ), "%s", value );
      } else if( !strcmp( line, "machine" ) ) {
        snprintf( machine_id, sizeof( machine_id ), "%s", value );
      } else if( !strcmp( line, "vsync" ) ) {
        vsync = strcmp( value, "0" ) != 0;
      } else if( !strcmp( line, "gl_linear" ) ) {
        linear = strcmp( value, "0" ) != 0;
      } else if( !strcmp( line, "border" ) ) {
        border = strcmp( value, "0" ) != 0;
      }
    }
    fclose( f );
  }
  if( machine_id[0] ) {
    settings_set_string( &settings_current.start_machine, machine_id );
    MOSDBG( "prefs machine=%s", machine_id );
  }
  if( filter[0] ) {
    if( settings_current.start_scaler_mode )
      libspectrum_free( settings_current.start_scaler_mode );
    settings_current.start_scaler_mode = utils_safe_strdup( filter );
    MOSDBG( "prefs filter=%s", filter );
  }
  morphosvideo_set_vsync( vsync );
  morphosvideo_set_linear_filter( linear );
  morphosdisplay_set_border( border );
  morphosvideo_set_mode( mode );
}

static void
save_video_preferences_file( const char *name )
{
  FILE *f = fopen( name, "w" );
  const char *mode;
  if( !f ) return;
  mode = morphosvideo_requested_mode() == MORPHOS_VIDEO_TINYGL ? "gl" :
         morphosvideo_requested_mode() == MORPHOS_VIDEO_OVERLAY ? "overlay" :
         "surface";
  fprintf( f, "video_mode=%s\n", mode );
  fprintf( f, "filter=%s\n",
           settings_current.start_scaler_mode ? settings_current.start_scaler_mode : "normal" );
  fprintf( f, "machine=%s\n",
           machine_current && machine_current->id ? machine_current->id :
           ( settings_current.start_machine ? settings_current.start_machine : "48" ) );
  fprintf( f, "vsync=%d\n", morphosvideo_vsync() ? 1 : 0 );
  fprintf( f, "gl_linear=%d\n", morphosvideo_linear_filter() ? 1 : 0 );
  fprintf( f, "border=%d\n", morphosdisplay_border_enabled() ? 1 : 0 );
#ifdef USE_JOYSTICK
  fprintf( f, "joy1_port=%d\n", morphosjoystick_get_port( 0 ) );
  fprintf( f, "joy2_port=%d\n", morphosjoystick_get_port( 1 ) );
#endif
  fclose( f );
}

static void
save_video_preferences( int persistent )
{
  save_video_preferences_file( "ENV:Fuse_MorphOS_prefs" );
  if( persistent ) save_video_preferences_file( "ENVARC:Fuse_MorphOS_prefs" );
}

static void
update_video_option_checks( void )
{
  update_video_menu_checks();
  if( video_vsync_item )
    SetAttrs( video_vsync_item, MUIA_Menuitem_Checked,
              morphosvideo_vsync() ? TRUE : FALSE, TAG_DONE );
  if( video_linear_item )
    SetAttrs( video_linear_item, MUIA_Menuitem_Checked,
              morphosvideo_linear_filter() ? TRUE : FALSE, TAG_DONE );
  if( video_border_item )
    SetAttrs( video_border_item, MUIA_Menuitem_Checked,
              morphosdisplay_border_enabled() ? TRUE : FALSE, TAG_DONE );
}

static void
toggle_video_vsync( int action )
{
  (void)action;
  morphosvideo_set_vsync( !morphosvideo_vsync() );
  update_video_option_checks();
  save_video_preferences( 0 );
}

static void
toggle_video_linear( int action )
{
  (void)action;
  morphosvideo_set_linear_filter( !morphosvideo_linear_filter() );
  update_video_option_checks();
  save_video_preferences( 0 );
  draw_video();
}

static void
toggle_video_border( int action )
{
  (void)action;
  if( morphosdisplay_set_border( !morphosdisplay_border_enabled() ) )
    return;
  update_video_option_checks();
  save_video_preferences( 1 );
  display_refresh_all();
  draw_video();
}

static void
update_input_menu_checks( void )
{
  int which, port;
  for( which = 0; which < 2; ++which ) {
    int selected = morphosjoystick_get_port( which );
    for( port = 0; port < 4; ++port ) {
      if( input_joy_port_item[which][port] )
        SetAttrs( (Object*)input_joy_port_item[which][port],
                  MUIA_Menuitem_Checked, selected == port ? TRUE : FALSE,
                  TAG_DONE );
    }
  }
  if( input_grab_mouse_item )
    SetAttrs( (Object*)input_grab_mouse_item, MUIA_Menuitem_Checked,
              ui_mouse_grabbed ? TRUE : FALSE, TAG_DONE );
}

static void
select_input_gamepad( int action )
{
  int which = action / 4;
  int port = action % 4;
  morphosjoystick_set_port( which, port );
  update_input_menu_checks();
  save_video_preferences( 0 );
}

static void
toggle_input_mouse_grab( int action )
{
  (void)action;
  if( ui_mouse_grabbed )
    ui_mouse_grabbed = ui_mouse_release( 0 );
  else
    ui_mouse_grabbed = ui_mouse_grab( 0 );
  update_input_menu_checks();
}

static void
save_all_settings( int action )
{
  menu_options_save( action );
  save_video_preferences( 1 );
}

static void
toggle_fullscreen( int action )
{
  (void)action;
  menu_options_fullscreen( 0 );
  morphosui_set_fullscreen( settings_current.full_screen );
}


static void
toolbar_tape_stop( int action )
{
  (void)action;
  tape_stop();
}

static void
toolbar_toggle_fast( int action )
{
  (void)action;
  if( settings_current.emulation_speed == 100 ) {
    if( fast_speed_saved <= 100 ) fast_speed_saved = 400;
    settings_current.emulation_speed = fast_speed_saved;
  } else {
    fast_speed_saved = settings_current.emulation_speed;
    settings_current.emulation_speed = 100;
  }
}

static Object *
make_filter_slider( LONG value )
{
#ifdef __MORPHOS__
  return SliderObject,
           MUIA_Numeric_Min, (IPTR)-100,
           MUIA_Numeric_Max, (IPTR)100,
           MUIA_Numeric_Value, (IPTR)value,
           MUIA_CycleChain, TRUE,
         End;
#else
  (void)value; return NULL;
#endif
}

static Object *
make_filter_label( const char *text )
{
#ifdef __MORPHOS__
  return TextObject,
           MUIA_Text_Contents, (IPTR)text,
           MUIA_Text_PreParse, (IPTR)"\33r",
           MUIA_Text_SetMin, TRUE,
         End;
#else
  (void)text; return NULL;
#endif
}


static int
modal_dispatch_common( ULONG id )
{
#ifdef __MORPHOS__
  if( id == (ULONG)MUIV_Application_ReturnID_Quit ) return 0;
  if( handle_poke_return_id( id ) ) return 1;
  if( handle_fixed_return_id( id ) ) return 1;
  if( id >= RID_MENU_BASE && id < RID_MENU_BASE + binding_count ) {
    morphos_menu_binding *b = &bindings[id - RID_MENU_BASE];
    if( b->callback ) b->callback( b->action );
    return 1;
  }
#else
  (void)id;
#endif
  return 0;
}

#ifdef __MORPHOS__
typedef struct morphos_tape_collect {
  char **lines;
  size_t count;
  size_t capacity;
} morphos_tape_collect;

static void
morphos_tape_collect_block( libspectrum_tape_block *block, void *user_data )
{
  morphos_tape_collect *c = (morphos_tape_collect*)user_data;
  char type[80], detail[120], line[220];
  char *copy;
  if( !c || c->count >= c->capacity ) return;
  type[0] = detail[0] = 0;
  libspectrum_tape_block_description( type, sizeof(type), block );
  tape_block_details( detail, sizeof(detail), block );
  if( detail[0] ) snprintf( line, sizeof(line), "%4lu  %-24s  %s",
                            (unsigned long)(c->count + 1), type, detail );
  else snprintf( line, sizeof(line), "%4lu  %s",
                 (unsigned long)(c->count + 1), type );
  copy = malloc( strlen(line) + 1 );
  if( !copy ) return;
  strcpy( copy, line );
  c->lines[c->count++] = copy;
}

static void
show_native_tape_browser( int action )
{
  Object *list, *view, *select, *close, *modified, *root, *buttons, *dialog;
  morphos_tape_collect c;
  char *lines[MORPHOS_TAPE_MAX_BLOCKS];
  int done = 0, quit_requested = 0, i;
  char modtext[96];
  (void)action;
  if( !app ) return;
  memset( &c, 0, sizeof(c) ); c.lines = lines; c.capacity = MORPHOS_TAPE_MAX_BLOCKS;
  if( tape_foreach( morphos_tape_collect_block, &c ) ) goto cleanup_lines;

  list = ListObject,
           InputListFrame,
           MUIA_List_AutoVisible, TRUE,
           MUIA_List_Active, tape_get_current_block(),
         End;
  view = ListviewObject, MUIA_Listview_List, (IPTR)list, End;
  select = make_text_button( LOCSTR( MSG_197 ), LOCSTR( MSG_198 ) );
  close = make_text_button( LOCSTR( MSG_194 ), LOCSTR( MSG_199 ) );
  snprintf( modtext, sizeof(modtext), "%s%s%lu",
            tape_modified ? "Tape modified - " : "Tape not modified - ",
            c.count >= MORPHOS_TAPE_MAX_BLOCKS ? "showing first " : "blocks: ",
            (unsigned long)c.count );
  modified = TextObject, MUIA_Text_Contents, (IPTR)modtext, End;
  buttons = HGroup, Child, (IPTR)HSpace(0), Child, (IPTR)select, Child, (IPTR)close, End;
  root = VGroup, Child, (IPTR)view, Child, (IPTR)modified, Child, (IPTR)buttons, End;
  dialog = WindowObject,
             MUIA_Window_Title, (IPTR)LOCSTR( MSG_196 ),
             MUIA_Window_ID, MAKE_ID('F','T','B','R'),
             MUIA_Window_RootObject, (IPTR)root,
           End;
  if( !dialog ) goto cleanup_lines;
  for( i = 0; i < (int)c.count; ++i )
    DoMethod( list, MUIM_List_InsertSingle, (IPTR)c.lines[i], MUIV_List_Insert_Bottom );
  DoMethod( select, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_TAPE_SELECT );
  DoMethod( close, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_TAPE_CLOSE );
  DoMethod( list, MUIM_Notify, MUIA_List_DoubleClick, TRUE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_TAPE_SELECT );
  DoMethod( dialog, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_TAPE_CLOSE );
  morphosui_dialog_begin();
  fuse_emulation_pause();
  DoMethod( (Object*)app, OM_ADDMEMBER, dialog );
  SetAttrs( dialog, MUIA_Window_Open, TRUE, TAG_DONE );
  while( !done && app ) {
    ULONG sig = 0, id = DoMethod( (Object*)app, MUIM_Application_NewInput, &sig );
    if( id == RID_TAPE_SELECT ) {
      LONG active = MUIV_List_Active_Off;
      GetAttr( MUIA_List_Active, list, (ULONG*)&active );
      if( active >= 0 && (size_t)active < c.count ) {
        tape_select_block_no_update( active );
        done = 1;
      }
    } else if( id == RID_TAPE_CLOSE ) done = 1;
    else if( id == RID_QUIT ) { quit_requested = 1; done = 1; }
    else if( !modal_dispatch_common( id ) && !id && sig ) Wait( sig );
  }
  SetAttrs( dialog, MUIA_Window_Open, FALSE, TAG_DONE );
  DoMethod( (Object*)app, OM_REMMEMBER, dialog );
  MUI_DisposeObject( dialog );
  fuse_emulation_unpause();
  morphosui_dialog_end();
  if( quit_requested ) morphos_request_quit(0);
cleanup_lines:
  for( i = 0; i < (int)c.count; ++i ) free( c.lines[i] );
}

static void
format_memory_line( char *out, size_t outlen, libspectrum_word addr )
{
  int col;
  size_t used = 0;
  int n = snprintf( out, outlen, "%04X: ", (unsigned)addr );

  if( n < 0 ) return;
  used = (size_t)n;
  for( col = 0; col < 8 && used + 4 < outlen; ++col ) {
    n = snprintf( out + used, outlen - used, "%02X ",
                  readbyte_internal( addr + col ) );
    if( n < 0 ) return;
    used += (size_t)n;
  }
  if( used + 10 >= outlen ) return;
  out[used++] = ' ';
  for( col = 0; col < 8 && used + 2 < outlen; ++col ) {
    int c = readbyte_internal( addr + col );
    out[used++] = ( c >= 32 && c < 127 ) ? (char)c : '.';
  }
  out[used] = 0;
}

static void
show_native_memory_browser( int action )
{
  enum { MEMORY_BROWSER_ROWS = 0x10000 / 8 };
  static libspectrum_word base;
  Object *list, *view, *addr, *prev, *next, *go, *close;
  Object *buttons, *goto_row, *root, *dialog = NULL;
  char *lines = NULL;
  int done = 0, quit_requested = 0;
  int row;
  (void)action;
  if( !app ) return;

  lines = (char*)calloc( MEMORY_BROWSER_ROWS, 48 );
  if( !lines ) {
    ui_error( UI_ERROR_ERROR, "Not enough memory for Memory Browser" );
    return;
  }

  list = ListObject,
           InputListFrame,
           MUIA_Font, MUIV_Font_Fixed,
         End;
  view = ListviewObject,
           MUIA_Listview_List, (IPTR)list,
         End;
  addr = StringObject, StringFrame, MUIA_String_MaxLen, 8,
           MUIA_Font, MUIV_Font_Fixed,
           MUIA_String_Contents, (IPTR)"", MUIA_CycleChain, TRUE, End;
  prev = make_text_button( LOCSTR( MSG_233 ), LOCSTR( MSG_201 ) );
  next = make_text_button( LOCSTR( MSG_234 ), LOCSTR( MSG_202 ) );
  go = make_text_button( LOCSTR( MSG_203 ), LOCSTR( MSG_204 ) );
  close = make_text_button( LOCSTR( MSG_194 ), LOCSTR( MSG_205 ) );
  if( !list || !view || !addr || !prev || !next || !go || !close ) goto cleanup;

  for( row = 0; row < MEMORY_BROWSER_ROWS; ++row ) {
    char *line = lines + row * 48;
    format_memory_line( line, 48, (libspectrum_word)( row * 8 ) );
    DoMethod( list, MUIM_List_InsertSingle, (IPTR)line, MUIV_List_Insert_Bottom );
  }

  goto_row = HGroup,
               Child, (IPTR)TextObject,
                 MUIA_Text_Contents, (IPTR)LOCSTR( MSG_206 ), End,
               Child, (IPTR)addr,
               Child, (IPTR)go,
             End;
  buttons = HGroup,
              Child, (IPTR)prev,
              Child, (IPTR)next,
              Child, (IPTR)HSpace(0),
              Child, (IPTR)close,
            End;
  root = VGroup,
           Child, (IPTR)goto_row,
           Child, (IPTR)view,
           Child, (IPTR)buttons,
         End;
  dialog = WindowObject,
             MUIA_Window_Title, (IPTR)LOCSTR( MSG_200 ),
             MUIA_Window_ID, MAKE_ID('F','M','E','M'),
             MUIA_Window_RootObject, (IPTR)root,
           End;
  if( !dialog ) goto cleanup;

  base &= 0xfff8;
  SetAttrs( list, MUIA_List_Active, (IPTR)( base / 8 ), TAG_DONE );
  DoMethod(prev,MUIM_Notify,MUIA_Pressed,FALSE,MUIV_Notify_Application,2,MUIM_Application_ReturnID,RID_MEM_PREV);
  DoMethod(next,MUIM_Notify,MUIA_Pressed,FALSE,MUIV_Notify_Application,2,MUIM_Application_ReturnID,RID_MEM_NEXT);
  DoMethod(go,MUIM_Notify,MUIA_Pressed,FALSE,MUIV_Notify_Application,2,MUIM_Application_ReturnID,RID_MEM_GOTO);
  DoMethod(addr,MUIM_Notify,MUIA_String_Acknowledge,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_Application_ReturnID,RID_MEM_GOTO);
  DoMethod(close,MUIM_Notify,MUIA_Pressed,FALSE,MUIV_Notify_Application,2,MUIM_Application_ReturnID,RID_MEM_CLOSE);
  DoMethod(dialog,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,MUIV_Notify_Application,2,MUIM_Application_ReturnID,RID_MEM_CLOSE);

  morphosui_dialog_begin();
  fuse_emulation_pause();
  DoMethod((Object*)app,OM_ADDMEMBER,dialog);
  SetAttrs(dialog,MUIA_Window_Open,TRUE,TAG_DONE);
  while(!done && app) {
    ULONG sig=0,id=DoMethod((Object*)app,MUIM_Application_NewInput,&sig);
    if(id==RID_MEM_PREV){
      base = (libspectrum_word)( base - 128 );
      base &= 0xfff8;
      SetAttrs( list, MUIA_List_Active, (IPTR)( base / 8 ), TAG_DONE );
    } else if(id==RID_MEM_NEXT){
      base = (libspectrum_word)( base + 128 );
      base &= 0xfff8;
      SetAttrs( list, MUIA_List_Active, (IPTR)( base / 8 ), TAG_DONE );
    } else if(id==RID_MEM_GOTO){
      ULONG v=0; char *end=NULL; unsigned long parsed; int radix=10;
      GetAttr(MUIA_String_Contents,addr,&v);
      if(v){
        const char *input=(const char*)v;
        if(input[0]=='0' && (input[1]=='x' || input[1]=='X')) radix=16;
        parsed=strtoul(input,&end,radix);
        if(end!=input && *end=='\0' && parsed<=0xffff){
          base=(libspectrum_word)parsed;
          base &= 0xfff8;
          SetAttrs( list, MUIA_List_Active, (IPTR)( base / 8 ), TAG_DONE );
        } else {
          ui_error(UI_ERROR_ERROR,"Invalid address: use 0..65535 or 0x0000..0xFFFF");
        }
      }
    } else if(id==RID_MEM_CLOSE) done=1;
    else if(id==RID_QUIT){quit_requested=1;done=1;}
    else if(!modal_dispatch_common(id) && !id && sig) Wait(sig);
  }
  SetAttrs(dialog,MUIA_Window_Open,FALSE,TAG_DONE);
  DoMethod((Object*)app,OM_REMMEMBER,dialog);
  MUI_DisposeObject(dialog);
  dialog = NULL;
  fuse_emulation_unpause();
  morphosui_dialog_end();
  if(quit_requested) morphos_request_quit(0);

cleanup:
  if( dialog ) MUI_DisposeObject( dialog );
  free( lines );
}

static size_t
fill_pokefinder_list( Object *list, int *pages, libspectrum_word *offsets )
{
  size_t page, off, count = 0;
  char line[96];

  if( !list ) return 0;
  DoMethod( list, MUIM_List_Clear );
  if( !pokefinder_is_allocated() ) return 0;

  if( pokefinder_count && pokefinder_count <= MORPHOS_POKE_MAX_POSSIBLE ) {
    for( page = 0;
         page < MEMORY_PAGES_IN_16K * SPECTRUM_RAM_PAGES &&
           count < MORPHOS_POKE_MAX_POSSIBLE;
         ++page ) {
      memory_page *mapping = &memory_map_ram[page];
      for( off = 0;
           off < MEMORY_PAGE_SIZE && count < MORPHOS_POKE_MAX_POSSIBLE;
           ++off ) {
        if( !( pokefinder_impossible[page][off / 8] & ( 1 << ( off & 7 ) ) ) ) {
          pages[count] = mapping->page_num;
          offsets[count] = mapping->offset + off;
          snprintf( line, sizeof( line ), "RAM %d : 0x%04X  value=%u",
                    pages[count], (unsigned)offsets[count],
                    (unsigned)mapping->page[off] );
          DoMethod( list, MUIM_List_InsertSingle, (IPTR)line,
                    MUIV_List_Insert_Bottom );
          ++count;
        }
      }
    }
  }
  return count;
}

static void
refresh_native_pokefinder( void )
{
  char countbuf[96];
  if( !poke_window || !poke_list || !poke_count_text ) return;

  poke_visible = fill_pokefinder_list( (Object*)poke_list, poke_pages,
                                       poke_offsets );
  snprintf( countbuf, sizeof( countbuf ), "Possible locations: %lu%s",
            (unsigned long)pokefinder_count,
            pokefinder_count > MORPHOS_POKE_MAX_POSSIBLE ?
              " (refine search to list them)" : "" );
  SetAttrs( (Object*)poke_count_text, MUIA_Text_Contents, (IPTR)countbuf,
            TAG_DONE );
}

static void
close_native_pokefinder( void )
{
  Object *dialog = (Object*)poke_window;
  if( !dialog ) return;

  poke_window = poke_list = poke_count_text = poke_entry = NULL;
  poke_visible = 0;
  SetAttrs( dialog, MUIA_Window_Open, FALSE, TAG_DONE );
  if( app ) DoMethod( (Object*)app, OM_REMMEMBER, dialog );
  MUI_DisposeObject( dialog );
  if( poke_overlay_guard ) {
    poke_overlay_guard = 0;
    morphosui_dialog_end();
  }
}

static int
parse_byte_value( const char *text, libspectrum_byte *value )
{
  char *end;
  unsigned long parsed;
  int base = 10;

  if( !text || !*text || !value ) return 0;
  if( text[0] == '0' && ( text[1] == 'x' || text[1] == 'X' ) ) base = 16;
  parsed = strtoul( text, &end, base );
  if( end == text || *end != '\0' || parsed > 255 ) return 0;
  *value = (libspectrum_byte)parsed;
  return 1;
}

static int
handle_poke_return_id( ULONG id )
{
  int refresh = 0;

  if( !poke_window ) return 0;

  switch( id ) {
  case RID_POKE_INC:
    pokefinder_incremented();
    refresh = 1;
    break;
  case RID_POKE_DEC:
    pokefinder_decremented();
    refresh = 1;
    break;
  case RID_POKE_RESET:
    pokefinder_clear();
    refresh = 1;
    break;
  case RID_POKE_SEARCH:
    if( poke_entry ) {
      ULONG raw = 0;
      libspectrum_byte value;
      GetAttr( MUIA_String_Contents, (Object*)poke_entry, &raw );
      if( raw && parse_byte_value( (const char*)raw, &value ) ) {
        pokefinder_search( value );
        refresh = 1;
      } else {
        ui_error( UI_ERROR_ERROR, "Invalid value: use 0..255" );
      }
    }
    break;
  case RID_POKE_BREAK:
    if( poke_list ) {
      LONG active = MUIV_List_Active_Off;
      GetAttr( MUIA_List_Active, (Object*)poke_list, (ULONG*)&active );
      if( active >= 0 && (size_t)active < poke_visible ) {
        debugger_breakpoint_add_address(
          DEBUGGER_BREAKPOINT_TYPE_WRITE, memory_source_ram,
          poke_pages[active], poke_offsets[active], 0,
          DEBUGGER_BREAKPOINT_LIFE_PERMANENT, NULL );
      }
    }
    break;
  case RID_POKE_CLOSE:
    close_native_pokefinder();
    return 1;
  default:
    return 0;
  }

  if( refresh ) refresh_native_pokefinder();
  return 1;
}

static void
show_native_pokefinder( int action )
{
  Object *view, *inc, *dec, *search, *reset, *brk, *close;
  Object *row, *buttons, *root, *dialog;
  (void)action;

  if( !app ) return;
  if( poke_window ) {
    SetAttrs( (Object*)poke_window, MUIA_Window_Open, TRUE,
              MUIA_Window_Activate, TRUE, TAG_DONE );
    return;
  }
  if( !pokefinder_is_allocated() ) pokefinder_clear();

  poke_list = ListObject,
                InputListFrame,
                MUIA_List_AutoVisible, TRUE,
                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
              End;
  if( !poke_list ) return;
  view = ListviewObject, MUIA_Listview_List, (IPTR)poke_list, End;
  poke_count_text = TextObject, MUIA_Text_Contents, (IPTR)"", End;
  poke_entry = StringObject, StringFrame, MUIA_String_MaxLen, 8,
                 MUIA_CycleChain, TRUE, End;
  inc = make_text_button( LOCSTR( MSG_208 ), LOCSTR( MSG_209 ) );
  dec = make_text_button( LOCSTR( MSG_210 ), LOCSTR( MSG_211 ) );
  search = make_text_button( LOCSTR( MSG_212 ), LOCSTR( MSG_213 ) );
  reset = make_text_button( LOCSTR( MSG_235 ), LOCSTR( MSG_214 ) );
  brk = make_text_button( LOCSTR( MSG_215 ),
                          LOCSTR( MSG_216 ) );
  close = make_text_button( LOCSTR( MSG_194 ), LOCSTR( MSG_217 ) );
  if( !view || !poke_count_text || !poke_entry || !inc || !dec || !search ||
      !reset || !brk || !close ) {
    /* Listview owns poke_list once constructed.  The remaining objects are
       still independent at this point, so dispose each successful object. */
    if( view ) MUI_DisposeObject( view );
    else if( poke_list ) MUI_DisposeObject( (Object*)poke_list );
    if( poke_count_text ) MUI_DisposeObject( (Object*)poke_count_text );
    if( poke_entry ) MUI_DisposeObject( (Object*)poke_entry );
    if( inc ) MUI_DisposeObject( inc );
    if( dec ) MUI_DisposeObject( dec );
    if( search ) MUI_DisposeObject( search );
    if( reset ) MUI_DisposeObject( reset );
    if( brk ) MUI_DisposeObject( brk );
    if( close ) MUI_DisposeObject( close );
    poke_list = poke_count_text = poke_entry = NULL;
    return;
  }

  row = HGroup,
          Child, (IPTR)TextObject,
            MUIA_Text_Contents, (IPTR)LOCSTR( MSG_218 ), End,
          Child, (IPTR)poke_entry,
          Child, (IPTR)search,
        End;
  buttons = HGroup,
              Child, (IPTR)inc, Child, (IPTR)dec, Child, (IPTR)reset,
              Child, (IPTR)brk, Child, (IPTR)HSpace( 0 ),
              Child, (IPTR)close,
            End;
  root = VGroup,
           Child, (IPTR)poke_count_text,
           Child, (IPTR)view,
           Child, (IPTR)row,
           Child, (IPTR)buttons,
         End;
  dialog = WindowObject,
             MUIA_Window_Title, (IPTR)LOCSTR( MSG_207 ),
             MUIA_Window_ID, MAKE_ID( 'F', 'P', 'K', 'F' ),
             MUIA_Window_RootObject, (IPTR)root,
           End;
  if( !dialog ) {
    MUI_DisposeObject( root );
    poke_list = poke_count_text = poke_entry = NULL;
    return;
  }

  poke_window = dialog;
  DoMethod( inc, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_INC );
  DoMethod( dec, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_DEC );
  DoMethod( search, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_SEARCH );
  DoMethod( (Object*)poke_entry, MUIM_Notify, MUIA_String_Acknowledge,
            MUIV_EveryTime, MUIV_Notify_Application, 2,
            MUIM_Application_ReturnID, RID_POKE_SEARCH );
  DoMethod( reset, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_RESET );
  DoMethod( brk, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_BREAK );
  DoMethod( close, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_CLOSE );
  DoMethod( dialog, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_POKE_CLOSE );

  /* Keep the emulator running while filtering values.  The overlay guard is
     intentionally held for the lifetime of this modeless tool so a hardware
     VLayer cannot cover the separate MUI window. */
  morphosui_dialog_begin();
  poke_overlay_guard = 1;
  DoMethod( (Object*)app, OM_ADDMEMBER, dialog );
  SetAttrs( dialog, MUIA_Window_Open, TRUE, TAG_DONE );
  refresh_native_pokefinder();
}

static void
format_debug_monitor( char *out, size_t outlen )
{
  size_t used = 0, i, len;
  libspectrum_word addr = debugger_disassembly_address;
  char dis[128];
  int n;

  n = snprintf( out, outlen,
                "PC %04X  SP %04X  AF %04X  BC %04X  DE %04X  HL %04X\n"
                "AF' %04X BC' %04X DE' %04X HL' %04X  IX %04X IY %04X\n"
                "I %02X R %02X IM %d IFF1 %d IFF2 %d  T %d\n\nDisassembly:\n",
                (unsigned)PC, (unsigned)SP, (unsigned)AF, (unsigned)BC,
                (unsigned)DE, (unsigned)HL, (unsigned)AF_, (unsigned)BC_,
                (unsigned)DE_, (unsigned)HL_, (unsigned)IX, (unsigned)IY,
                (unsigned)I, (unsigned)( ( R & 0x7f ) | ( R7 & 0x80 ) ),
                IM, IFF1, IFF2, tstates );
  if( n < 0 ) return;
  used = (size_t)n;

  for( i = 0; i < 12 && used + 140 < outlen; ++i ) {
    size_t b;
    debugger_disassemble( dis, sizeof( dis ), &len, addr );
    n = snprintf( out + used, outlen - used, "%04X: %-36s",
                  (unsigned)addr, dis );
    if( n < 0 ) break;
    used += (size_t)n;

    for( b = 0; b < len && used + 4 < outlen; ++b ) {
      n = snprintf( out + used, outlen - used, " %02X",
                    readbyte_internal( addr + b ) );
      if( n < 0 ) break;
      used += (size_t)n;
    }
    if( used + 2 < outlen ) {
      out[used++] = '\n';
      out[used] = 0;
    }
    addr += len ? len : 1;
  }
}

int
morphosui_debugger_update( void )
{
#ifdef __MORPHOS__
  char buffer[8192];
  if( !debugger_text ) return 0;
  format_debug_monitor( buffer, sizeof( buffer ) );
  SetAttrs( (Object*)debugger_text, MUIA_Text_Contents, (IPTR)buffer, TAG_DONE );
#endif
  return 0;
}

int
morphosui_debugger_disassemble( libspectrum_word address )
{
  debugger_disassembly_address = address;
  return morphosui_debugger_update();
}

int
morphosui_debugger_deactivate( int interruptible )
{
  (void)interruptible;
  debugger_close_requested = 1;
  return 0;
}

int
morphosui_debugger_activate( void )
{
#ifdef __MORPHOS__
  Object *text, *command, *refresh, *exec, *close, *row, *buttons, *root, *dialog;
  char buffer[8192];
  int quit_requested = 0;

  if( !app ) return 1;
  if( debugger_window ) {
    SetAttrs( (Object*)debugger_window, MUIA_Window_Open, TRUE,
              MUIA_Window_Activate, TRUE, TAG_DONE );
    return 0;
  }

  if( fuse_emulation_pause() ) return 1;
  debugger_close_requested = 0;
  debugger_disassembly_address = PC;
  format_debug_monitor( buffer, sizeof( buffer ) );

  text = TextObject,
           TextFrame,
           MUIA_Font, MUIV_Font_Fixed,
           MUIA_Text_Contents, (IPTR)buffer,
           MUIA_Text_PreParse, (IPTR)"\33l",
         End;
  command = StringObject,
              StringFrame,
              MUIA_Font, MUIV_Font_Fixed,
              MUIA_String_MaxLen, 256,
              MUIA_CycleChain, TRUE,
            End;
  refresh = make_text_button( LOCSTR( MSG_220 ), LOCSTR( MSG_221 ) );
  exec = make_text_button( LOCSTR( MSG_222 ), LOCSTR( MSG_223 ) );
  close = make_text_button( LOCSTR( MSG_095 ), LOCSTR( MSG_224 ) );
  if( !text || !command || !refresh || !exec || !close ) {
    if( text ) MUI_DisposeObject( text );
    if( command ) MUI_DisposeObject( command );
    if( refresh ) MUI_DisposeObject( refresh );
    if( exec ) MUI_DisposeObject( exec );
    if( close ) MUI_DisposeObject( close );
    debugger_run();
    fuse_emulation_unpause();
    return 1;
  }

  row = HGroup,
          Child, (IPTR)TextObject,
            MUIA_Text_Contents, (IPTR)LOCSTR( MSG_225 ), End,
          Child, (IPTR)command,
          Child, (IPTR)exec,
        End;
  buttons = HGroup,
              Child, (IPTR)refresh,
              Child, (IPTR)HSpace( 0 ),
              Child, (IPTR)close,
            End;
  root = VGroup,
           Child, (IPTR)text,
           Child, (IPTR)row,
           Child, (IPTR)buttons,
         End;
  dialog = WindowObject,
             MUIA_Window_Title, (IPTR)LOCSTR( MSG_219 ),
             MUIA_Window_ID, MAKE_ID( 'F', 'D', 'B', 'G' ),
             MUIA_Window_RootObject, (IPTR)root,
           End;
  if( !dialog ) {
    MUI_DisposeObject( root );
    debugger_run();
    fuse_emulation_unpause();
    return 1;
  }

  debugger_window = dialog;
  debugger_text = text;
  debugger_command = command;
  DoMethod( refresh, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID,
            RID_DEBUG_REFRESH );
  DoMethod( exec, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID,
            RID_DEBUG_COMMAND );
  DoMethod( command, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID,
            RID_DEBUG_COMMAND );
  DoMethod( close, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID,
            RID_DEBUG_CLOSE );
  DoMethod( dialog, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID,
            RID_DEBUG_CLOSE );

  morphosui_dialog_begin();
  DoMethod( (Object*)app, OM_ADDMEMBER, dialog );
  SetAttrs( dialog, MUIA_Window_Open, TRUE, TAG_DONE );

  while( !debugger_close_requested && app ) {
    ULONG sig = 0;
    ULONG id = DoMethod( (Object*)app, MUIM_Application_NewInput, &sig );
    if( id == RID_DEBUG_REFRESH ) {
      morphosui_debugger_update();
    } else if( id == RID_DEBUG_COMMAND ) {
      ULONG raw = 0;
      GetAttr( MUIA_String_Contents, command, &raw );
      if( raw && *(const char*)raw ) {
        debugger_command_evaluate( (const char*)raw );
        SetAttrs( command, MUIA_String_Contents, (IPTR)"", TAG_DONE );
        if( !debugger_close_requested ) morphosui_debugger_update();
      }
    } else if( id == RID_DEBUG_CLOSE ) {
      /* debugger_run() also calls ui_debugger_deactivate(), which sets the
         close flag through the MorphOS bridge. */
      debugger_run();
    } else if( id == RID_QUIT ) {
      quit_requested = 1;
      debugger_close_requested = 1;
    } else if( !modal_dispatch_common( id ) && !id && sig ) {
      Wait( sig );
    }
  }

  SetAttrs( dialog, MUIA_Window_Open, FALSE, TAG_DONE );
  DoMethod( (Object*)app, OM_REMMEMBER, dialog );
  debugger_window = debugger_text = debugger_command = NULL;
  MUI_DisposeObject( dialog );
  morphosui_dialog_end();
  fuse_emulation_unpause();

  if( quit_requested ) morphos_request_quit( 0 );
  return 0;
#else
  return 1;
#endif
}

static void
show_native_debug_monitor( int action )
{
  (void)action;
  debugger_mode = DEBUGGER_MODE_HALTED;
  morphosui_debugger_activate();
}

#endif

static int
scaler_uses_composite_filter_options( scaler_type scaler )
{
  switch( scaler ) {
  case SCALER_PALTV2X:
  case SCALER_PALTV3X:
  case SCALER_PALTV4X:
  case SCALER_NTSC2X:
  case SCALER_NTSC3X:
  case SCALER_NTSC4X:
    return 1;
  default:
    return 0;
  }
}

static void
show_filter_options( int action )
{
#ifdef __MORPHOS__
  const char *names[10] = {
    LOCSTR( MSG_180 ), LOCSTR( MSG_181 ), LOCSTR( MSG_182 ), LOCSTR( MSG_183 ), LOCSTR( MSG_184 ),
    LOCSTR( MSG_185 ), LOCSTR( MSG_186 ), LOCSTR( MSG_187 ), LOCSTR( MSG_188 ), LOCSTR( MSG_189 )
  };
  static const LONG defaults[10] = { 0, -27, 21, 10, -10, 0, -16, -59, 14, -71 };
  int *values[10] = {
    &settings_current.composite_filter_hue,
    &settings_current.composite_filter_saturation,
    &settings_current.composite_filter_contrast,
    &settings_current.composite_filter_brightness,
    &settings_current.composite_filter_sharpness,
    &settings_current.composite_filter_gamma,
    &settings_current.composite_filter_resolution,
    &settings_current.composite_filter_artifacts,
    &settings_current.composite_filter_fringing,
    &settings_current.composite_filter_bleed
  };
  Object *dialog, *root, *grid, *buttons, *apply, *defs, *close, *note;
  Object *sliders[10], *labels[10];
  char label_text[10][48];
  int i, done = 0, quit_requested = 0;
  (void)action;
  if( !app ) return;

  for( i = 0; i < 10; ++i ) {
    snprintf( label_text[i], sizeof( label_text[i] ), "%s:", names[i] );
    sliders[i] = make_filter_slider( *values[i] );
    labels[i] = make_filter_label( label_text[i] );
    if( !sliders[i] || !labels[i] ) return;
  }

  grid = ColGroup( 2 ),
           MUIA_Group_Spacing, 6,
           Child, (IPTR)labels[0], Child, (IPTR)sliders[0],
           Child, (IPTR)labels[1], Child, (IPTR)sliders[1],
           Child, (IPTR)labels[2], Child, (IPTR)sliders[2],
           Child, (IPTR)labels[3], Child, (IPTR)sliders[3],
           Child, (IPTR)labels[4], Child, (IPTR)sliders[4],
           Child, (IPTR)labels[5], Child, (IPTR)sliders[5],
           Child, (IPTR)labels[6], Child, (IPTR)sliders[6],
           Child, (IPTR)labels[7], Child, (IPTR)sliders[7],
           Child, (IPTR)labels[8], Child, (IPTR)sliders[8],
           Child, (IPTR)labels[9], Child, (IPTR)sliders[9],
         End;
  if( !grid ) return;

  apply = make_text_button( LOCSTR( MSG_190 ), LOCSTR( MSG_191 ) );
  defs  = make_text_button( LOCSTR( MSG_192 ), LOCSTR( MSG_193 ) );
  close = make_text_button( LOCSTR( MSG_194 ), LOCSTR( MSG_195 ) );
  if( !apply || !defs || !close ) return;
  buttons = HGroup,
              Child, (IPTR)HSpace( 0 ),
              Child, (IPTR)apply,
              Child, (IPTR)defs,
              Child, (IPTR)close,
              Child, (IPTR)HSpace( 0 ),
            End;
  note = TextObject,
           MUIA_Text_Contents, (IPTR)( scaler_uses_composite_filter_options( current_scaler )
             ? LOCSTR( MSG_260 )
             : LOCSTR( MSG_261 ) ),
           MUIA_Text_PreParse, (IPTR)"\33c",
           MUIA_Text_SetMin, FALSE,
         End;
  root = VGroup,
           MUIA_Group_Spacing, 6,
           Child, (IPTR)note,
           Child, (IPTR)grid,
           Child, (IPTR)buttons,
         End;
  dialog = WindowObject,
             MUIA_Window_Title, (IPTR)LOCSTR( MSG_179 ),
             MUIA_Window_ID, MAKE_ID('F','F','L','T'),
             MUIA_Window_RootObject, (IPTR)root,
           End;
  if( !dialog ) return;

  DoMethod( apply, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_FILTER_APPLY );
  DoMethod( defs, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_FILTER_DEFAULTS );
  DoMethod( close, MUIM_Notify, MUIA_Pressed, FALSE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_FILTER_CLOSE );
  for( i = 0; i < 10; ++i )
    DoMethod( sliders[i], MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime,
              MUIV_Notify_Application, 2, MUIM_Application_ReturnID,
              RID_FILTER_LIVE_BASE + i );
  DoMethod( dialog, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_FILTER_CLOSE );

  morphosui_dialog_begin();
  DoMethod( (Object*)app, OM_ADDMEMBER, dialog );
  SetAttrs( dialog, MUIA_Window_Open, TRUE, TAG_DONE );
  while( !done && app ) {
    ULONG signals = 0, id = DoMethod( (Object*)app, MUIM_Application_NewInput, &signals );
    if( id == (ULONG)MUIV_Application_ReturnID_Quit ) id = 0;
    if( id == RID_FILTER_APPLY ) {
      for( i = 0; i < 10; ++i ) { ULONG v = 0; GetAttr( MUIA_Numeric_Value, sliders[i], &v ); *values[i] = (LONG)v; }
      display_refresh_all();
      display_frame();
    } else if( id >= RID_FILTER_LIVE_BASE && id < RID_FILTER_LIVE_BASE + 10 ) {
      i = (int)( id - RID_FILTER_LIVE_BASE );
      { ULONG v = 0; GetAttr( MUIA_Numeric_Value, sliders[i], &v ); *values[i] = (LONG)v; }
      /* The standard GTK dialog updates these settings live.  Our modal MUI
         loop must also render a frame here, otherwise display_refresh_all()
         only becomes visible after the dialog is closed. */
      display_refresh_all();
      display_frame();
    } else if( id == RID_FILTER_DEFAULTS ) {
      for( i = 0; i < 10; ++i ) SetAttrs( sliders[i], MUIA_Numeric_Value, (IPTR)defaults[i], TAG_DONE );
      for( i = 0; i < 10; ++i ) *values[i] = defaults[i];
      display_refresh_all();
      display_frame();
    } else if( id == RID_FILTER_CLOSE ) {
      done = 1;
    } else if( id == RID_QUIT ) {
      quit_requested = 1; done = 1;
    } else if( handle_fixed_return_id( id ) ) {
      /* handled */
    } else if( id >= RID_MENU_BASE && id < RID_MENU_BASE + binding_count ) {
      morphos_menu_binding *b = &bindings[id - RID_MENU_BASE];
      if( b->callback && b->callback != show_filter_options ) b->callback( b->action );
    } else if( !id && signals ) {
      Wait( signals );
    }
  }
  SetAttrs( dialog, MUIA_Window_Open, FALSE, TAG_DONE );
  DoMethod( (Object*)app, OM_REMMEMBER, dialog );
  MUI_DisposeObject( dialog );
  morphosui_dialog_end();
  if( quit_requested ) morphos_request_quit( 0 );
#else
  (void)action;
#endif
}

static void
show_native_about( int action )
{
#ifdef __MORPHOS__
  Object *about_window;
  int quit_requested = 0;
  (void)action;
  if( !app ) return;
  morphosui_dialog_begin();
  about_window = AboutboxObject,
                   MUIA_Window_Title, (IPTR)LOCSTR( MSG_032 ),
                   MUIA_Aboutbox_Credits, (IPTR)LOCSTR( MSG_086 ),
                   MUIA_Aboutbox_Build, (IPTR)VERSION " MorphOS",
                   MUIA_Aboutbox_LogoFallbackMode,
                     MUIV_Aboutbox_LogoFallbackMode_Auto,
                 End;
  if( about_window ) {
    DoMethod( (Object*)app, OM_ADDMEMBER, about_window );
    SetAttrs( about_window, MUIA_Window_Open, TRUE, TAG_DONE );
    while( app ) {
      ULONG signals = 0;
      ULONG id = DoMethod( (Object*)app, MUIM_Application_NewInput, &signals );
      ULONG open = FALSE;
      GetAttr( MUIA_Window_Open, about_window, &open );
      if( !open ) break;
      if( id == RID_QUIT ) { quit_requested = 1; break; }
      if( id == (ULONG)MUIV_Application_ReturnID_Quit ) id = 0;
      if( id == RID_APPDROP ) morphos_handle_app_drop();
      else if( id == RID_AREXX_RESET ) menu_machine_reset( 0 );
      else if( id == RID_AREXX_HARDRESET ) menu_machine_reset( 1 );
      else if( id == RID_AREXX_PAUSE ) menu_machine_pause( 0 );
      else if( id == RID_AREXX_FULLSCREEN ) toggle_fullscreen( 0 );
      else if( id >= RID_MENU_BASE && id < RID_MENU_BASE + binding_count ) {
        morphos_menu_binding *b = &bindings[id - RID_MENU_BASE];
        if( b->callback && b->callback != show_native_about ) b->callback( b->action );
      } else if( !id && signals ) {
        Wait( signals );
      }
    }
    SetAttrs( about_window, MUIA_Window_Open, FALSE, TAG_DONE );
    DoMethod( (Object*)app, OM_REMMEMBER, about_window );
    MUI_DisposeObject( about_window );
  }
  morphosui_dialog_end();
  if( quit_requested ) morphos_request_quit( 0 );
#else
  (void)action;
#endif
}

static void
update_control_state( void )
{
#ifdef __MORPHOS__
  int tape_here = tape_present();
  int plus3 = machine_current &&
              ( machine_current->capabilities &
                LIBSPECTRUM_MACHINE_CAPABILITY_PLUS3_DISK );
  fdd_t *fa = plus3 ? specplus3_get_fdd( SPECPLUS3_DRIVE_A ) : NULL;
  fdd_t *fb = plus3 ? specplus3_get_fdd( SPECPLUS3_DRIVE_B ) : NULL;

  if( control_tape_play )
    SetAttrs( (Object*)control_tape_play, MUIA_Disabled,
              tape_here ? FALSE : TRUE, TAG_DONE );
  if( control_tape_stop )
    SetAttrs( (Object*)control_tape_stop, MUIA_Disabled,
              ( tape_here && tape_is_playing() ) ? FALSE : TRUE, TAG_DONE );
  if( control_tape_rewind )
    SetAttrs( (Object*)control_tape_rewind, MUIA_Disabled,
              tape_here ? FALSE : TRUE, TAG_DONE );
  if( control_disk_a_eject )
    SetAttrs( (Object*)control_disk_a_eject, MUIA_Disabled,
              ( fa && fa->loaded ) ? FALSE : TRUE, TAG_DONE );
  if( control_disk_b_eject )
    SetAttrs( (Object*)control_disk_b_eject, MUIA_Disabled,
              ( fb && fb->loaded ) ? FALSE : TRUE, TAG_DONE );
#endif
}

static Object *
add_drive_menu( Object *parent, const char *title, const char *path,
                int drive, int allow_flip )
{
  Object *d, *sub;
  char p[192];
  d = add_menu_node_path( parent, title, path );
  if( !d ) return NULL;

#define DRIVE_ACTION(label,suffix,cb,val) do { \
  snprintf( p, sizeof( p ), "%s/%s", path, suffix ); \
  add_action_path( d, label, p, cb, val ); \
} while(0)

  DRIVE_ACTION( LOCSTR( MSG_109 ), "Insert New", menu_media_insert_new, drive );
  DRIVE_ACTION( LOCSTR( MSG_017 ), "Insert...", menu_media_insert, drive );
  DRIVE_ACTION( LOCSTR( MSG_018 ), "Eject", menu_media_eject, drive );
  DRIVE_ACTION( LOCSTR( MSG_019 ), "Save", menu_media_save, drive );
  DRIVE_ACTION( LOCSTR( MSG_020 ), "Save As...", menu_media_save, 0x100 | drive );
  if( allow_flip ) {
    snprintf( p, sizeof( p ), "%s/Flip disk", path );
    sub = add_menu_node_path( d, LOCSTR( MSG_108 ), p );
    snprintf( p, sizeof( p ), "%s/Flip disk/Turn upside down", path );
    add_action_path( sub, LOCSTR( MSG_171 ), p, menu_media_flip, 0x100 | drive );
    snprintf( p, sizeof( p ), "%s/Flip disk/Turn back", path );
    add_action_path( sub, LOCSTR( MSG_170 ), p, menu_media_flip, drive );
  }
  snprintf( p, sizeof( p ), "%s/Write protect", path );
  sub = add_menu_node_path( d, LOCSTR( MSG_175 ), p );
  snprintf( p, sizeof( p ), "%s/Write protect/Enable", path );
  add_action_path( sub, LOCSTR( MSG_105 ), p, menu_media_writeprotect, 0x100 | drive );
  snprintf( p, sizeof( p ), "%s/Write protect/Disable", path );
  add_action_path( sub, LOCSTR( MSG_102 ), p, menu_media_writeprotect, drive );
#undef DRIVE_ACTION
  return d;
}

static Object *
add_ide_menu( Object *parent, const char *title, const char *path, int unit )
{
  Object *d;
  char p[192];
  d = add_menu_node_path( parent, title, path );
  if( !d ) return NULL;
  snprintf( p, sizeof( p ), "%s/Insert...", path );
  add_action_path( d, LOCSTR( MSG_017 ), p, menu_media_ide_insert, unit );
  snprintf( p, sizeof( p ), "%s/Commit", path );
  add_action_path( d, LOCSTR( MSG_094 ), p, menu_media_ide_commit, unit );
  snprintf( p, sizeof( p ), "%s/Eject", path );
  add_action_path( d, LOCSTR( MSG_018 ), p, menu_media_ide_eject, unit );
  return d;
}

void
menu_machine_pause( int action )
{
#ifdef __MORPHOS__
  ULONG signals = 0;
  ULONG id;
  (void)action;

  if( paused_native ) {
    paused_native = 0;
    ui_statusbar_update( UI_STATUSBAR_ITEM_PAUSED,
                         UI_STATUSBAR_STATE_INACTIVE );
    timer_estimate_reset();
    fuse_emulation_unpause();
    return;
  }

  if( fuse_emulation_pause() ) return;
  paused_native = 1;
  ui_statusbar_update( UI_STATUSBAR_ITEM_PAUSED, UI_STATUSBAR_STATE_ACTIVE );

  /* Keep MUI alive while Spectrum execution is paused. A second Pause action
     enters this function recursively, clears paused_native and lets this loop
     return to the emulator. */
  while( paused_native && !fuse_exiting && app ) {
    id = DoMethod( (Object*)app, MUIM_Application_NewInput, &signals );
    if( id == (ULONG)MUIV_Application_ReturnID_Quit ) id = 0;
    if( id == RID_QUIT ) {
      morphos_request_quit( 0 );
      break;
    }
    if( handle_poke_return_id( id ) ) continue;
    if( id == RID_APPDROP ) { morphos_handle_app_drop(); continue; }
    if( id == RID_AREXX_RESET ) { menu_machine_reset( 0 ); continue; }
    if( id == RID_AREXX_HARDRESET ) { menu_machine_reset( 1 ); continue; }
    if( id == RID_AREXX_FULLSCREEN ) { toggle_fullscreen( 0 ); continue; }
    if( id == RID_AREXX_PAUSE ) { menu_machine_pause( 0 ); continue; }
    if( id >= RID_MENU_BASE && id < RID_MENU_BASE + binding_count ) {
      morphos_menu_binding *b = &bindings[id - RID_MENU_BASE];
      if( b->callback ) b->callback( b->action );
      continue;
    }
    if( !id && signals ) {
      Wait( signals );
      signals = 0;
    }
  }

  if( paused_native ) {
    paused_native = 0;
    ui_statusbar_update( UI_STATUSBAR_ITEM_PAUSED,
                         UI_STATUSBAR_STATE_INACTIVE );
    fuse_emulation_unpause();
  }
#else
  (void)action;
#endif
}

static Object *
create_menu( void )
{
  Object *strip, *m, *sub, *sub2;
  int i;
  char title[32], path[160];

  binding_count = 0;
  menu_object_count = 0;
  strip = MUI_NewObject( MUIC_Menustrip, TAG_DONE );
  if( !strip ) return NULL;

  m = add_menu_node_path( strip, LOCSTR( MSG_226 ), "/File" );
  add_action_path( m, LOCSTR( MSG_001 ), "/File/Open...", menu_file_open, 0 );
  add_action_path( m, LOCSTR( MSG_002 ), "/File/Save Snapshot...", menu_file_savesnapshot, 0 );

  sub = add_menu_node_path( m, LOCSTR( MSG_033 ), "/File/Recording" );
  add_action_path( sub, LOCSTR( MSG_139 ), "/File/Recording/Record...", menu_file_recording_record, 0 );
  add_action_path( sub, LOCSTR( MSG_136 ), "/File/Recording/Record from snapshot...", menu_file_recording_recordfromsnapshot, 0 );
  add_action_path( sub, LOCSTR( MSG_096 ), "/File/Recording/Continue recording...", menu_file_recording_continuerecording, 0 );
  add_separator( sub );
  add_action_path( sub, LOCSTR( MSG_110 ), "/File/Recording/Insert snapshot", menu_file_recording_insertsnapshot, 0 );
  add_action_path( sub, LOCSTR( MSG_140 ), "/File/Recording/Rollback", menu_file_recording_rollback, 0 );
  add_action_path( sub, LOCSTR( MSG_141 ), "/File/Recording/Rollback to...", menu_file_recording_rollbackto, 0 );
  add_separator( sub );
  add_action_path( sub, LOCSTR( MSG_129 ), "/File/Recording/Play...", menu_file_recording_play, 0 );
  add_action_path( sub, LOCSTR( MSG_012 ), "/File/Recording/Stop", menu_file_recording_stop, 0 );
  add_action_path( sub, LOCSTR( MSG_107 ), "/File/Recording/Finalise...", menu_file_recording_finalise, 0 );

  sub = add_menu_node_path( m, LOCSTR( MSG_090 ), "/File/AY Logging" );
  add_action_path( sub, LOCSTR( MSG_139 ), "/File/AY Logging/Record...", menu_file_aylogging_record, 0 );
  add_action_path( sub, LOCSTR( MSG_012 ), "/File/AY Logging/Stop", menu_file_aylogging_stop, 0 );

  sub = add_menu_node_path( m, LOCSTR( MSG_034 ), "/File/Screenshot" );
  add_action_path( sub, LOCSTR( MSG_122 ), "/File/Screenshot/Open SCR Screenshot...", menu_file_screenshot_openscrscreenshot, 0 );
  add_action_path( sub, LOCSTR( MSG_146 ), "/File/Screenshot/Save Screen as SCR...", menu_file_screenshot_savescreenasscr, 0 );
  add_action_path( sub, LOCSTR( MSG_121 ), "/File/Screenshot/Open MLT Screenshot...", menu_file_screenshot_openmltscreenshot, 0 );
  add_action_path( sub, LOCSTR( MSG_144 ), "/File/Screenshot/Save Screen as MLT...", menu_file_screenshot_savescreenasmlt, 0 );
#ifdef USE_LIBPNG
  add_action_path( sub, LOCSTR( MSG_145 ), "/File/Screenshot/Save Screen as PNG...", menu_file_screenshot_savescreenaspng, 0 );
#endif
#ifdef HAVE_LIB_XML2
  sub = add_menu_node_path( m, LOCSTR( MSG_143 ), "/File/Scalable Vector Graphics" );
  add_action_path( sub, LOCSTR( MSG_163 ), "/File/Scalable Vector Graphics/Start capture in line mode...", menu_file_scalablevectorgraphics_startcaptureinlinemode, 0 );
  add_action_path( sub, LOCSTR( MSG_162 ), "/File/Scalable Vector Graphics/Start capture in dot mode...", menu_file_scalablevectorgraphics_startcaptureindotmode, 0 );
  add_action_path( sub, LOCSTR( MSG_164 ), "/File/Scalable Vector Graphics/Stop capture", menu_file_scalablevectorgraphics_stopcapture, 0 );
#endif
  sub = add_menu_node_path( m, LOCSTR( MSG_035 ), "/File/Movie" );
  add_action_path( sub, LOCSTR( MSG_139 ), "/File/Movie/Record...", menu_file_movie_record, 0 );
  add_action_path( sub, LOCSTR( MSG_135 ), "/File/Movie/Record from RZX...", menu_file_movie_record_recordfromrzx, 0 );
  add_action_path( sub, LOCSTR( MSG_005 ), "/File/Movie/Pause", menu_file_movie_pause, 0 );
  add_action_path( sub, LOCSTR( MSG_095 ), "/File/Movie/Continue", menu_file_movie_pause, 0 );
  add_action_path( sub, LOCSTR( MSG_012 ), "/File/Movie/Stop", menu_file_movie_stop, 0 );
  add_separator( m );
  add_action_path( m, LOCSTR( MSG_114 ), "/File/Load binary data...", menu_file_loadbinarydata, 0 );
  add_action_path( m, LOCSTR( MSG_147 ), "/File/Save binary data...", menu_file_savebinarydata, 0 );
  add_separator( m );
  add_action_path( m, LOCSTR( MSG_229 ), "/File/About...", show_native_about, 0 );
  {
    Object *quit_item = add_action_path( m, LOCSTR( MSG_230 ), "/File/Quit",
                                         morphos_request_quit, 0 );
    if( quit_item )
      SetAttrs( quit_item, MUIA_Menuitem_Shortcut, (IPTR)"Q", TAG_DONE );
  }

  m = add_menu_node_path( strip, LOCSTR( MSG_004 ), "/Machine" );
  add_action_path( m, LOCSTR( MSG_005 ), "/Machine/Pause...", menu_machine_pause, 0 );
  add_action_path( m, LOCSTR( MSG_006 ), "/Machine/Reset...", menu_machine_reset, 0 );
  add_action_path( m, LOCSTR( MSG_007 ), "/Machine/Hard reset...", menu_machine_reset, 1 );
  sub = add_menu_node_path( m, LOCSTR( MSG_228 ), "/Machine/Select" );
  /* machine_types[] is populated later by STARTUP_MANAGER_MODULE_MACHINE.
     display_init()->ui_init() runs before that module, so building the
     entries here would leave this submenu permanently empty.  Keep the
     container and populate it from morphosui_populate_machine_menu() after
     startup_manager_run() has completed. */
  machine_select_menu = sub;
  machine_menu_count = 0;
  add_separator( m );
  add_action_path( m, LOCSTR( MSG_053 ), "/Machine/Debugger...", show_native_debug_monitor, 0 );
  add_action_path( m, LOCSTR( MSG_054 ), "/Machine/Poke Finder...", show_native_pokefinder, 0 );
  add_action_path( m, LOCSTR( MSG_055 ), "/Machine/Poke Memory...", menu_machine_pokememory, 0 );
  add_action_path( m, LOCSTR( MSG_056 ), "/Machine/Memory Browser...", show_native_memory_browser, 0 );
  sub = add_menu_node_path( m, LOCSTR( MSG_058 ), "/Machine/Profiler" );
  add_action_path( sub, LOCSTR( MSG_161 ), "/Machine/Profiler/Start", menu_machine_profiler_start, 0 );
  add_action_path( sub, LOCSTR( MSG_012 ), "/Machine/Profiler/Stop", menu_machine_profiler_stop, 0 );
  add_separator( m );
  add_action_path( m, LOCSTR( MSG_057 ), "/Machine/NMI", menu_machine_nmi, 0 );
  add_action_path( m, LOCSTR( MSG_120 ), "/Machine/Multiface Red Button", menu_machine_multifaceredbutton, 0 );
  add_action_path( m, LOCSTR( MSG_101 ), "/Machine/Didaktik SNAP", menu_machine_didaktiksnap, 0 );

  m = add_menu_node_path( strip, LOCSTR( MSG_009 ), "/Media" );
  sub = add_menu_node_path( m, LOCSTR( MSG_010 ), "/Media/Tape" );
  add_action_path( sub, LOCSTR( MSG_001 ), "/Media/Tape/Open...", menu_media_tape_open, 0 );
  add_action_path( sub, LOCSTR( MSG_011 ), "/Media/Tape/Play", menu_media_tape_play, 0 );
  add_action_path( sub, LOCSTR( MSG_093 ), "/Media/Tape/Browse...", show_native_tape_browser, 0 );
  add_action_path( sub, LOCSTR( MSG_013 ), "/Media/Tape/Rewind", menu_media_tape_rewind, 0 );
  add_action_path( sub, LOCSTR( MSG_014 ), "/Media/Tape/Clear", menu_media_tape_clear, 0 );
  add_action_path( sub, LOCSTR( MSG_015 ), "/Media/Tape/Write...", menu_media_tape_write, 0 );
  add_action_path( sub, LOCSTR( MSG_137 ), "/Media/Tape/Record Start", menu_media_tape_recordstart, 0 );
  add_action_path( sub, LOCSTR( MSG_138 ), "/Media/Tape/Record Stop", menu_media_tape_recordstop, 0 );

  sub = add_menu_node_path( m, LOCSTR( MSG_044 ), "/Media/Interface 1" );
  {
    const char *const microdrive_titles[8] = {
      LOCSTR( MSG_239 ), LOCSTR( MSG_240 ), LOCSTR( MSG_241 ), LOCSTR( MSG_242 ),
      LOCSTR( MSG_243 ), LOCSTR( MSG_244 ), LOCSTR( MSG_245 ), LOCSTR( MSG_246 )
    };
    for( i = 1; i <= 8; i++ ) {
      snprintf( path, sizeof( path ), "/Media/Interface 1/Microdrive %d", i );
      add_drive_menu( sub, microdrive_titles[i - 1], path, 0x30 + i, 0 );
    }
  }
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_133 ), "/Media/Interface 1/RS232" );
  add_action_path( sub2, LOCSTR( MSG_130 ), "/Media/Interface 1/RS232/Plug RxD", menu_media_if1_rs232, 0x01 );
  add_action_path( sub2, LOCSTR( MSG_173 ), "/Media/Interface 1/RS232/Unplug RxD", menu_media_if1_rs232, 0x11 );
  add_action_path( sub2, LOCSTR( MSG_131 ), "/Media/Interface 1/RS232/Plug TxD", menu_media_if1_rs232, 0x02 );
  add_action_path( sub2, LOCSTR( MSG_174 ), "/Media/Interface 1/RS232/Unplug TxD", menu_media_if1_rs232, 0x12 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_151 ), "/Media/Interface 1/Sinclair NET" );
  add_action_path( sub2, LOCSTR( MSG_132 ), "/Media/Interface 1/Sinclair NET/Plug in", menu_media_if1_rs232, 0x03 );
  add_action_path( sub2, LOCSTR( MSG_172 ), "/Media/Interface 1/Sinclair NET/Unplug", menu_media_if1_rs232, 0x13 );

  sub = add_menu_node_path( m, LOCSTR( MSG_016 ), "/Media/Disk" );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_087 ), "/Media/Disk/+3" );
  add_drive_menu( sub2, LOCSTR( MSG_247 ), "/Media/Disk/+3/Drive A:", 0x01, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_248 ), "/Media/Disk/+3/Drive B:", 0x02, 1 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_091 ), "/Media/Disk/Beta" );
  add_drive_menu( sub2, LOCSTR( MSG_247 ), "/Media/Disk/Beta/Drive A:", 0x11, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_248 ), "/Media/Disk/Beta/Drive B:", 0x12, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_249 ), "/Media/Disk/Beta/Drive C:", 0x13, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_250 ), "/Media/Disk/Beta/Drive D:", 0x14, 1 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_088 ), "/Media/Disk/+D" );
  add_drive_menu( sub2, LOCSTR( MSG_251 ), "/Media/Disk/+D/Drive 1", 0x21, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_252 ), "/Media/Disk/+D/Drive 2", 0x22, 1 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_123 ), "/Media/Disk/Opus" );
  add_drive_menu( sub2, LOCSTR( MSG_251 ), "/Media/Disk/Opus/Drive 1", 0x41, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_252 ), "/Media/Disk/Opus/Drive 2", 0x42, 1 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_097 ), "/Media/Disk/DISCiPLE" );
  add_drive_menu( sub2, LOCSTR( MSG_251 ), "/Media/Disk/DISCiPLE/Drive 1", 0x51, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_252 ), "/Media/Disk/DISCiPLE/Drive 2", 0x52, 1 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_099 ), "/Media/Disk/Didaktik 80" );
  add_drive_menu( sub2, LOCSTR( MSG_253 ), "/Media/Disk/Didaktik 80/Drive A", 0x61, 1 );
  add_drive_menu( sub2, LOCSTR( MSG_254 ), "/Media/Disk/Didaktik 80/Drive B", 0x62, 1 );

  sub = add_menu_node_path( m, LOCSTR( MSG_047 ), "/Media/Cartridge" );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_166 ), "/Media/Cartridge/Timex Dock" );
  add_action_path( sub2, LOCSTR( MSG_017 ), "/Media/Cartridge/Timex Dock/Insert...", menu_media_cartridge_timexdock_insert, 0 );
  add_action_path( sub2, LOCSTR( MSG_018 ), "/Media/Cartridge/Timex Dock/Eject", menu_media_cartridge_timexdock_eject, 0 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_045 ), "/Media/Cartridge/Interface 2" );
  add_action_path( sub2, LOCSTR( MSG_017 ), "/Media/Cartridge/Interface 2/Insert...", menu_media_cartridge_interface2_insert, 0 );
  add_action_path( sub2, LOCSTR( MSG_018 ), "/Media/Cartridge/Interface 2/Eject", menu_media_cartridge_interface2_eject, 0 );

  sub = add_menu_node_path( m, LOCSTR( MSG_046 ), "/Media/IDE" );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_150 ), "/Media/IDE/Simple 8-bit" );
  add_ide_menu( sub2, LOCSTR( MSG_255 ), "/Media/IDE/Simple 8-bit/Master", 1 );
  add_ide_menu( sub2, LOCSTR( MSG_256 ), "/Media/IDE/Simple 8-bit/Slave", 2 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_176 ), "/Media/IDE/ZXATASP" );
  add_ide_menu( sub2, LOCSTR( MSG_255 ), "/Media/IDE/ZXATASP/Master", 3 );
  add_ide_menu( sub2, LOCSTR( MSG_256 ), "/Media/IDE/ZXATASP/Slave", 4 );
  add_ide_menu( sub, LOCSTR( MSG_257 ), "/Media/IDE/ZXCF CompactFlash", 5 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_104 ), "/Media/IDE/DivIDE" );
  add_ide_menu( sub2, LOCSTR( MSG_255 ), "/Media/IDE/DivIDE/Master", 6 );
  add_ide_menu( sub2, LOCSTR( MSG_256 ), "/Media/IDE/DivIDE/Slave", 7 );
  add_ide_menu( sub, LOCSTR( MSG_258 ), "/Media/IDE/DivMMC", 8 );
  add_ide_menu( sub, LOCSTR( MSG_259 ), "/Media/IDE/ZXMMC", 9 );

  m = add_menu_node_path( strip, LOCSTR( MSG_022 ), "/Video" );
  video_surface_item = add_video_action( m, LOCSTR( MSG_023 ), MORPHOS_VIDEO_SURFACE, 1 );
  video_overlay_item = add_video_action( m, LOCSTR( MSG_024 ), MORPHOS_VIDEO_OVERLAY,
                                         morphosvideo_has_overlay() );
  video_tinygl_item = add_video_action( m, LOCSTR( MSG_025 ), MORPHOS_VIDEO_TINYGL,
                                        morphosvideo_has_tinygl() );
  add_separator( m );
  sub = add_menu_node_path( m, LOCSTR( MSG_227 ), "/Video/Scaler" );
  for( i = 0; i < SCALER_NUM; ++i ) {
    Object *item = add_action( sub, scaler_name( (scaler_type)i ),
                               select_scaler_menu, i );
    scaler_menu_item[i] = item;
    if( item ) SetAttrs( item, MUIA_Menuitem_Checkit, TRUE, TAG_DONE );
  }
  morphosui_update_scaler_menu();
  add_action_path( m, LOCSTR( MSG_179 ), "/Video/Filter options...",
                   show_filter_options, 0 );
  add_separator( m );
  fullscreen_item = add_action( m, LOCSTR( MSG_232 ), toggle_fullscreen, 0 );
  if( fullscreen_item ) {
    SetAttrs( fullscreen_item,
              MUIA_Menuitem_Checkit, TRUE,
              MUIA_Menuitem_Checked, settings_current.full_screen ? TRUE : FALSE,
              MUIA_Menuitem_Shortcut, (IPTR)"F",
              TAG_DONE );
  }
  video_border_item = add_action( m, LOCSTR( MSG_231 ), toggle_video_border, 0 );
  if( video_border_item )
    SetAttrs( video_border_item, MUIA_Menuitem_Checkit, TRUE,
              MUIA_Menuitem_Checked, morphosdisplay_border_enabled() ? TRUE : FALSE,
              TAG_DONE );
  video_vsync_item = add_action( m, LOCSTR( MSG_026 ), toggle_video_vsync, 0 );
  if( video_vsync_item ) SetAttrs( video_vsync_item, MUIA_Menuitem_Checkit, TRUE, TAG_DONE );
  video_linear_item = add_action( m, LOCSTR( MSG_027 ), toggle_video_linear, 0 );
  if( video_linear_item ) SetAttrs( video_linear_item, MUIA_Menuitem_Checkit, TRUE,
                                    MUIA_Menuitem_Enabled, morphosvideo_has_tinygl(), TAG_DONE );
  update_video_option_checks();

  m = add_menu_node_path( strip, LOCSTR( MSG_029 ), "/Input" );
#ifdef USE_JOYSTICK
  {
    const char *const joystick_titles[2] = { LOCSTR( MSG_237 ), LOCSTR( MSG_238 ) };
    const char *const gamepad_titles[4] = {
      LOCSTR( MSG_040 ), LOCSTR( MSG_041 ), LOCSTR( MSG_042 ), LOCSTR( MSG_043 )
    };
    int which, port;
    int have_lowlevel = morphosjoystick_available();
    for( which = 0; which < 2; ++which ) {
      snprintf( path, sizeof( path ), "/Input/Joystick %d", which + 1 );
      sub = add_menu_node_path( m, joystick_titles[which], path );
      for( port = 0; port < 4; ++port ) {
        Object *item;
        /* MUI keeps MUIA_Menuitem_Title by pointer.  Never pass the local
           title[] scratch buffer here: it becomes invalid after create_menu(). */
        item = add_action( sub, gamepad_titles[port],
                           select_input_gamepad, which * 4 + port );
        input_joy_port_item[which][port] = item;
        if( item ) SetAttrs( item, MUIA_Menuitem_Checkit, TRUE,
                             MUIA_Menuitem_Enabled, have_lowlevel ? TRUE : FALSE,
                             TAG_DONE );
      }
    }
  }
#endif
  add_separator( m );
  input_grab_mouse_item = add_action( m, LOCSTR( MSG_030 ), toggle_input_mouse_grab, 0 );
  if( input_grab_mouse_item )
    SetAttrs( input_grab_mouse_item, MUIA_Menuitem_Checkit, TRUE, TAG_DONE );
  update_input_menu_checks();

  m = add_menu_node_path( strip, LOCSTR( MSG_021 ), "/Options" );
  add_action_path( m, LOCSTR( MSG_048 ), "/Options/General...", menu_options_general, 0 );
  add_action_path( m, LOCSTR( MSG_049 ), "/Options/Media...", menu_options_media, 0 );
  add_action_path( m, LOCSTR( MSG_050 ), "/Options/Sound...", menu_options_sound, 0 );
  sub = add_menu_node_path( m, LOCSTR( MSG_036 ), "/Options/Peripherals" );
  add_action_path( sub, LOCSTR( MSG_048 ), "/Options/Peripherals/General...", menu_options_peripherals_general, 0 );
  add_action_path( sub, LOCSTR( MSG_050 ), "/Options/Peripherals/Sound...", menu_options_peripherals_sound, 0 );
  add_action_path( sub, LOCSTR( MSG_103 ), "/Options/Peripherals/Disk...", menu_options_peripherals_disk, 0 );
  add_action_path( m, LOCSTR( MSG_134 ), "/Options/RZX...", menu_options_rzx, 0 );
  add_action_path( m, LOCSTR( MSG_116 ), "/Options/Movie...", menu_options_movie, 0 );
  sub = add_menu_node_path( m, LOCSTR( MSG_037 ), "/Options/Joysticks" );
#ifdef USE_JOYSTICK
  add_action_path( sub, LOCSTR( MSG_038 ), "/Options/Joysticks/Joystick 1...", menu_options_joysticks_select, 1 );
  add_action_path( sub, LOCSTR( MSG_039 ), "/Options/Joysticks/Joystick 2...", menu_options_joysticks_select, 2 );
#endif
  add_action_path( sub, LOCSTR( MSG_113 ), "/Options/Joysticks/Keyboard...", menu_options_joysticks_select, 3 );
  sub = add_menu_node_path( m, LOCSTR( MSG_149 ), "/Options/Select ROMs" );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_115 ), "/Options/Select ROMs/Machine ROMs" );
  add_action( sub2, LOCSTR( MSG_158 ), menu_options_selectroms_machine_select, 1 );
  add_action( sub2, LOCSTR( MSG_159 ), menu_options_selectroms_machine_select, 2 );
  add_action( sub2, LOCSTR( MSG_157 ), menu_options_selectroms_machine_select, 3 );
  add_action( sub2, LOCSTR( MSG_153 ), menu_options_selectroms_machine_select, 4 );
  add_action( sub2, LOCSTR( MSG_154 ), menu_options_selectroms_machine_select, 5 );
  add_action( sub2, LOCSTR( MSG_155 ), menu_options_selectroms_machine_select, 6 );
  add_action( sub2, LOCSTR( MSG_156 ), menu_options_selectroms_machine_select, 7 );
  add_action( sub2, LOCSTR( MSG_167 ), menu_options_selectroms_machine_select, 8 );
  add_action( sub2, LOCSTR( MSG_168 ), menu_options_selectroms_machine_select, 9 );
  add_action( sub2, LOCSTR( MSG_169 ), menu_options_selectroms_machine_select, 10 );
  add_action( sub2, LOCSTR( MSG_126 ), menu_options_selectroms_machine_select, 11 );
  add_action( sub2, LOCSTR( MSG_127 ), menu_options_selectroms_machine_select, 12 );
  add_action( sub2, LOCSTR( MSG_125 ), menu_options_selectroms_machine_select, 13 );
  add_action( sub2, LOCSTR( MSG_148 ), menu_options_selectroms_machine_select, 14 );
  add_action( sub2, LOCSTR( MSG_160 ), menu_options_selectroms_machine_select, 15 );
  sub2 = add_menu_node_path( sub, LOCSTR( MSG_128 ), "/Options/Select ROMs/Peripheral ROMs" );
  add_action( sub2, LOCSTR( MSG_111 ), menu_options_selectroms_peripheral_select, 1 );
  add_action( sub2, LOCSTR( MSG_092 ), menu_options_selectroms_peripheral_select, 2 );
  add_action( sub2, LOCSTR( MSG_089 ), menu_options_selectroms_peripheral_select, 3 );
  add_action( sub2, LOCSTR( MSG_100 ), menu_options_selectroms_peripheral_select, 4 );
  add_action( sub2, LOCSTR( MSG_098 ), menu_options_selectroms_peripheral_select, 5 );
  add_action( sub2, LOCSTR( MSG_119 ), menu_options_selectroms_peripheral_select, 6 );
  add_action( sub2, LOCSTR( MSG_117 ), menu_options_selectroms_peripheral_select, 7 );
  add_action( sub2, LOCSTR( MSG_118 ), menu_options_selectroms_peripheral_select, 8 );
  add_action( sub2, LOCSTR( MSG_124 ), menu_options_selectroms_peripheral_select, 9 );
  add_action( sub2, LOCSTR( MSG_142 ), menu_options_selectroms_peripheral_select, 10 );
  add_action( sub2, LOCSTR( MSG_152 ), menu_options_selectroms_peripheral_select, 11 );
  add_action( sub2, LOCSTR( MSG_165 ), menu_options_selectroms_peripheral_select, 12 );
  add_action( sub2, LOCSTR( MSG_177 ), menu_options_selectroms_peripheral_select, 13 );
  add_action( sub2, LOCSTR( MSG_178 ), menu_options_selectroms_peripheral_select, 14 );
  add_action_path( m, LOCSTR( MSG_051 ), "/Options/Disk options...", menu_options_diskoptions, 0 );
  add_separator( m );
  add_action_path( m, LOCSTR( MSG_052 ), "/Options/Save", save_all_settings, 0 );

  m = add_menu_node_path( strip, LOCSTR( MSG_031 ), "/Help" );
  add_action_path( m, LOCSTR( MSG_112 ), "/Help/Keyboard...", menu_help_keyboard, 0 );

  return strip;
}

static int
morphosui_point_in_video_area( Object *obj, int x, int y )
{
  if( !obj ) return 0;
  return x >= _mleft( obj ) && y >= _mtop( obj ) &&
         x < _mleft( obj ) + _mwidth( obj ) &&
         y < _mtop( obj ) + _mheight( obj );
}

static ULONG
handle_intui_message( Object *obj, struct IntuiMessage *msg )
{
  unsigned raw;

  if( !msg ) return 0;

  switch( msg->Class ) {
  case IDCMP_RAWKEY:
    raw = msg->Code & 0x7f;
    if( raw == RAWKEY_Q &&
        ( msg->Qualifier & ( IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND ) ) ) {
      if( !( msg->Code & IECODE_UP_PREFIX ) ) morphos_request_quit( 0 );
      return MUI_EventHandlerRC_Eat;
    }
    if( raw == RAWKEY_F &&
        ( msg->Qualifier & ( IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND ) ) ) {
      /* Native MorphOS fullscreen accelerator.  Consume both key down and
         key up so Amiga+F never leaks an 'F' into the emulated keyboard. */
      if( !( msg->Code & IECODE_UP_PREFIX ) ) toggle_fullscreen( 0 );
      return MUI_EventHandlerRC_Eat;
    }
    /* Match the GTK frontend's host accelerators. Function keys are not part
       of the Spectrum keyboard, so consuming both press and release is safe. */
    switch( raw ) {
    case RAWKEY_F2: case RAWKEY_F3: case RAWKEY_F4: case RAWKEY_F5:
    case RAWKEY_F6: case RAWKEY_F7: case RAWKEY_F8: case RAWKEY_F9:
    case RAWKEY_F10: case RAWKEY_F11: case RAWKEY_INSERT: case RAWKEY_DELETE:
    case RAWKEY_PAUSE:
      if( !( msg->Code & IECODE_UP_PREFIX ) ) {
        switch( raw ) {
        case RAWKEY_F2: menu_file_savesnapshot( 0 ); break;
        case RAWKEY_F3: menu_file_open( 0 ); break;
        case RAWKEY_F4: menu_options_general( 0 ); break;
        case RAWKEY_F5: menu_machine_reset( 0 ); break;
        case RAWKEY_F6: menu_media_tape_write( 0 ); break;
        case RAWKEY_F7: menu_media_tape_open( 0 ); break;
        case RAWKEY_F8: menu_media_tape_play( 0 ); break;
        case RAWKEY_F9: break; /* Native machine selection is in Machine -> Select machine. */
        case RAWKEY_F10: morphos_request_quit( 0 ); break;
        case RAWKEY_F11: toggle_fullscreen( 0 ); break;
        case RAWKEY_INSERT: menu_file_recording_insertsnapshot( 0 ); break;
        case RAWKEY_DELETE: menu_file_recording_rollback( 0 ); break;
        case RAWKEY_PAUSE: menu_machine_pause( 0 ); break;
        }
      }
      return MUI_EventHandlerRC_Eat;
    default: break;
    }
    morphoskeyboard_key( msg->Code, msg->Qualifier,
                         !( msg->Code & IECODE_UP_PREFIX ) );
    return MUI_EventHandlerRC_Eat;

  case IDCMP_MOUSEBUTTONS: {
    struct Window *w = morphosui_native_window();
    int x = w ? w->MouseX : msg->MouseX;
    int y = w ? w->MouseY : msg->MouseY;
    int over_video = morphosui_point_in_video_area( obj, x, y );

    /* The Area event handler is registered on the whole MUI window.  Never
       eat mouse clicks belonging to toolbar/status/dialog gadgets.  This was
       why the top-panel buttons appeared dead until a resize changed MUI's
       event/layout ordering. */
    if( !over_video && !settings_current.kempston_mouse &&
        msg->Code == SELECTDOWN )
      last_video_click_valid = 0;
    if( !over_video && !mouse_grabbed_native ) return 0;

    /* When the Kempston mouse is not connected the emulated machine has no
       use for host mouse clicks.  Use a left-button double click on the video
       area as a native window/fullscreen toggle instead.  Intuition supplies
       the click timestamps, so this follows the user's system double-click
       interval rather than hard-coding one. */
    if( over_video && !settings_current.kempston_mouse &&
        msg->Code == SELECTDOWN ) {
      if( last_video_click_valid &&
          DoubleClick( last_video_click_seconds, last_video_click_micros,
                       msg->Seconds, msg->Micros ) ) {
        last_video_click_valid = 0;
        toggle_fullscreen( 0 );
        return MUI_EventHandlerRC_Eat;
      }
      last_video_click_seconds = msg->Seconds;
      last_video_click_micros = msg->Micros;
      last_video_click_valid = 1;
      return MUI_EventHandlerRC_Eat;
    }
    if( over_video && !settings_current.kempston_mouse &&
        msg->Code == SELECTUP )
      return MUI_EventHandlerRC_Eat;

    if( settings_current.kempston_mouse ) last_video_click_valid = 0;

    switch( msg->Code ) {
    case SELECTDOWN: ui_mouse_button( 1, 1 ); return MUI_EventHandlerRC_Eat;
    case SELECTUP:   ui_mouse_button( 1, 0 ); return MUI_EventHandlerRC_Eat;
    case MIDDLEDOWN: ui_mouse_button( 2, 1 ); return MUI_EventHandlerRC_Eat;
    case MIDDLEUP:   ui_mouse_button( 2, 0 ); return MUI_EventHandlerRC_Eat;
    case MENUDOWN:
      if( mouse_grabbed_native ) {
        ui_mouse_button( 3, 1 );
        return MUI_EventHandlerRC_Eat;
      }
      break;
    case MENUUP:
      if( mouse_grabbed_native ) {
        ui_mouse_button( 3, 0 );
        return MUI_EventHandlerRC_Eat;
      }
      break;
    default: break;
    }
    break;
  }

  case IDCMP_MOUSEMOVE: {
    struct Window *w = morphosui_native_window();
    int x = w ? w->MouseX : msg->MouseX;
    int y = w ? w->MouseY : msg->MouseY;
    if( mouse_grabbed_native && mouse_valid )
      ui_mouse_motion( x - last_mouse_x, y - last_mouse_y );
    last_mouse_x = x;
    last_mouse_y = y;
    mouse_valid = 1;
    break;
  }

  case IDCMP_ACTIVEWINDOW:
    mouse_valid = 0;
    ui_mouse_resume();
    break;

  case IDCMP_INACTIVEWINDOW:
    mouse_valid = 0;
    morphoskeyboard_release_all();
    ui_mouse_suspend();
    break;

  case IDCMP_NEWSIZE:
    /* Follow the MorphOS openMSX frontend: resizing changes presenter
       geometry only.  Do not upload/render a frame from the NEWSIZE event;
       MUI will issue MUIM_Draw and the emulator's normal frame path also
       presents independently.  Rendering here doubled the work during a
       resize and made Overlay/TinyGL drag noticeably sluggish. */
    if( obj && video_attached )
      morphosvideo_set_geometry( _mleft( obj ), _mtop( obj ),
                                 _mwidth( obj ), _mheight( obj ) );
    mouse_valid = 0;
    break;

  case IDCMP_REFRESHWINDOW:
    draw_video();
    break;
  }
  return 0;
}

static ULONG area_dispatcher( void );
static ULONG
area_dispatcher_real( struct IClass *cl, Object *obj, Msg msg )
{
  AreaData *data = (AreaData*)INST_DATA( cl, obj );

  switch( msg->MethodID ) {
  case OM_NEW:
    obj = (Object*)DoSuperMethodA( cl, obj, msg );
    if( obj ) memset( INST_DATA( cl, obj ), 0, sizeof( AreaData ) );
    return (ULONG)obj;

  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *ask = (struct MUIP_AskMinMax*)msg;
    int resizable;
    DoSuperMethodA( cl, obj, msg );
    resizable = morphosvideo_requested_mode() != MORPHOS_VIDEO_SURFACE;

    /* Same policy as openMSX MorphOS for a non-resizable video canvas:
       Surface is pixel-exact, while Overlay may be resized freely because
       CGXVideo performs the scaling. */
    if( !resizable ) {
      ask->MinMaxInfo->MinWidth = ask->MinMaxInfo->DefWidth =
        ask->MinMaxInfo->MaxWidth = logical_width;
      ask->MinMaxInfo->MinHeight = ask->MinMaxInfo->DefHeight =
        ask->MinMaxInfo->MaxHeight = logical_height;
    } else {
      ask->MinMaxInfo->MinWidth = logical_width < 160 ? logical_width : 160;
      ask->MinMaxInfo->MinHeight = logical_height < 120 ? logical_height : 120;
      ask->MinMaxInfo->DefWidth = logical_width;
      ask->MinMaxInfo->DefHeight = logical_height;
      ask->MinMaxInfo->MaxWidth = MUI_MAXMAX;
      ask->MinMaxInfo->MaxHeight = MUI_MAXMAX;
    }
    return 0;
  }

  case MUIM_Show: {
    ULONG r;
    MOSDBG( "Area MUIM_Show enter obj=%08lx winobj=%08lx", MOSPTR( obj ), MOSPTR( _win( obj ) ) );
    r = DoSuperMethodA( cl, obj, msg );
    MOSDBG( "Area MUIM_Show super rc=%lu", r );
    if( r ) {
      data = (AreaData*)INST_DATA( cl, obj );
      data->ehnode.ehn_Object = obj;
      data->ehnode.ehn_Class = cl;
      data->ehnode.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                                IDCMP_RAWKEY | IDCMP_NEWSIZE |
                                IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW |
                                IDCMP_REFRESHWINDOW;
      DoMethod( _win( obj ), MUIM_Window_AddEventHandler, &data->ehnode );
      data->added = 1;
      native_window = _window( obj );
      MOSDBG( "Area MUIM_Show native_window=%08lx geom=%ld,%ld %lux%lu",
              MOSPTR( native_window ),
              (LONG)_mleft( obj ), (LONG)_mtop( obj ),
              (ULONG)_mwidth( obj ), (ULONG)_mheight( obj ) );
      if( native_window ) SetMouseQueue( native_window, 256 );
    }
    MOSDBG( "Area MUIM_Show leave native_window=%08lx", MOSPTR( native_window ) );
    return r;
  }

  case MUIM_Hide:
    MOSDBG( "Area MUIM_Hide enter obj=%08lx native_window=%08lx attached=%ld",
            MOSPTR( obj ), MOSPTR( native_window ), (LONG)video_attached );
    if( data->added ) {
      DoMethod( _win( obj ), MUIM_Window_RemEventHandler, &data->ehnode );
      data->added = 0;
    }
    /* Invalidate all presenter state before MUI releases the native Window. */
    morphosvideo_detach();
    video_attached = 0;
    native_window = NULL;
    MOSDBG( "Area MUIM_Hide leave" );
    break;

  case MUIM_HandleEvent:
    return handle_intui_message( obj,
      ((struct MUIP_HandleEvent*)msg)->imsg );

  case MUIM_Draw:
    if( morphos_draw_trace_count < 12 )
      MOSDBG( "Area MUIM_Draw #%lu obj=%08lx native_window=%08lx",
              (ULONG)morphos_draw_trace_count, MOSPTR( obj ), MOSPTR( native_window ) );
    DoSuperMethodA( cl, obj, msg );
    draw_video();
    if( morphos_draw_trace_count < 12 ) morphos_draw_trace_count++;
    return 0;
  }

  return DoSuperMethodA( cl, obj, msg );
}

static ULONG
area_dispatcher( void )
{
  return area_dispatcher_real( (struct IClass*)REG_A0,
                               (Object*)REG_A2, (Msg)REG_A1 );
}

static struct EmulLibEntry area_gate = {
  TRAP_LIB, 0, (void(*)())area_dispatcher
};
#endif

static int
draw_video( void )
{
  static int busy = 0;
  int rc;
  if( ui_shutting_down ) return 0;
  if( busy ) {
    MOSDBG( "draw_video REENTRY SUPPRESSED" );
    return 0;
  }
  busy = 1;
  rc = draw_video_real();
  busy = 0;
  return rc;
}

static int
draw_video_real( void )
{
#ifdef __MORPHOS__
  Object *a = (Object*)area;
  struct Window *w;
  unsigned aw, ah;
  int left, top;

  if( !a || !last_pixels || !last_width || !last_height ) {
    if( morphos_draw_trace_count < 12 )
      MOSDBG( "draw_video skip a=%08lx pixels=%08lx src=%lux%lu",
              MOSPTR( a ), MOSPTR( last_pixels ), (ULONG)last_width, (ULONG)last_height );
    return 0;
  }
  w = morphosui_native_window();
  if( !w || !w->RPort ) {
    if( morphos_draw_trace_count < 12 )
      MOSDBG( "draw_video skip window=%08lx rport=%08lx", MOSPTR( w ), MOSPTR( w ? w->RPort : NULL ) );
    return 0;
  }
  aw = _mwidth( a ); ah = _mheight( a );
  if( !aw || !ah ) return 0;
  left = _mleft( a ); top = _mtop( a );

  if( morphos_draw_trace_count < 12 )
    MOSDBG( "draw_video window=%08lx rport=%08lx geom=%ld,%ld %lux%lu src=%lux%lu attached=%ld",
            MOSPTR( w ), MOSPTR( w->RPort ), (LONG)left, (LONG)top,
            (ULONG)aw, (ULONG)ah, (ULONG)last_width, (ULONG)last_height,
            (LONG)video_attached );

  if( !video_attached ) {
    MOSDBG( "draw_video presenter attach begin mode=%ld", (LONG)morphosvideo_requested_mode() );
    if( !morphosvideo_attach( w, left, top, aw, ah ) ) {
      MOSDBG( "draw_video presenter attach FAILED" );
      return 1;
    }
    video_attached = 1;
    MOSDBG( "draw_video presenter attach OK active=%s", morphosvideo_active_mode_name() );
    update_video_menu_checks();
  } else {
    morphosvideo_set_geometry( left, top, aw, ah );
    /* Overlay already owns a front buffer. During MUIM_Draw/NEWSIZE, changing
       VLayer indents is enough for CGXVideo to rescale that existing frame.
       Do not upload/swap/wait for the same 320x240 image again. */
    if( morphosvideo_redraw_without_upload() ) {
      if( morphos_draw_trace_count < 12 )
        MOSDBG( "draw_video Overlay geometry-only redraw" );
      return 0;
    }
  }

  /* Do not clear the full MUI Area here. Surface writes the whole fixed
     canvas, TinyGL clears its 3D target before the full-area blit, and Overlay
     owns its black bars.  The old extra FillPixelArray doubled redraw traffic
     during interactive resize. */
  if( !morphosvideo_present( last_pixels, last_pitch ) ) {
    MOSDBG( "draw_video present FAILED active=%s", morphosvideo_active_mode_name() );
    return 1;
  }
  if( morphos_draw_trace_count < 12 )
    MOSDBG( "draw_video present OK active=%s", morphosvideo_active_mode_name() );
  update_video_menu_checks();
#else
  return 1;
#endif
  return 0;
}

int
morphosui_menu_item_set_active( const char *path, int active )
{
#ifdef __MORPHOS__
  unsigned i;
  if( !path ) return 1;
  for( i = 0; i < menu_object_count; i++ ) {
    if( !strcmp( menu_objects[i].path, path ) ) {
      if( menu_objects[i].is_menu )
        SetAttrs( (Object*)menu_objects[i].object, MUIA_Menu_Enabled,
                  active ? TRUE : FALSE, TAG_DONE );
      else
        SetAttrs( (Object*)menu_objects[i].object, MUIA_Menuitem_Enabled,
                  active ? TRUE : FALSE, TAG_DONE );
      return 0;
    }
  }
#else
  (void)path; (void)active;
#endif
  return 1;
}

static void
set_status_text( void *object, const char *text )
{
#ifdef __MORPHOS__
  if( object ) SetAttrs( (Object*)object, MUIA_Text_Contents, (IPTR)text, TAG_DONE );
#else
  (void)object; (void)text;
#endif
}

int
morphosui_statusbar_update( ui_statusbar_item item, ui_statusbar_state state )
{
  const char *text = NULL;
  void *object = NULL;

  switch( item ) {
  case UI_STATUSBAR_ITEM_DISK:
    object = status_disk;
    text = state == UI_STATUSBAR_STATE_NOT_AVAILABLE ? "Disk:-" :
           state == UI_STATUSBAR_STATE_ACTIVE ? LOCSTR( MSG_073 ) : LOCSTR( MSG_072 );
    break;
  case UI_STATUSBAR_ITEM_MICRODRIVE:
    object = status_mdr;
    text = state == UI_STATUSBAR_STATE_NOT_AVAILABLE ? "MDR:-" :
           state == UI_STATUSBAR_STATE_ACTIVE ? LOCSTR( MSG_075 ) : LOCSTR( MSG_074 );
    break;
  case UI_STATUSBAR_ITEM_MOUSE:
    object = status_mouse;
    text = state == UI_STATUSBAR_STATE_ACTIVE ? LOCSTR( MSG_069 ) : LOCSTR( MSG_068 );
    break;
  case UI_STATUSBAR_ITEM_PAUSED:
    object = status_pause;
    text = state == UI_STATUSBAR_STATE_ACTIVE ? LOCSTR( MSG_067 ) : LOCSTR( MSG_066 );
    break;
  case UI_STATUSBAR_ITEM_TAPE:
    object = status_tape;
    text = state == UI_STATUSBAR_STATE_NOT_AVAILABLE ? "Tape:-" :
           state == UI_STATUSBAR_STATE_ACTIVE ? LOCSTR( MSG_071 ) : LOCSTR( MSG_070 );
    break;
  default:
    return 1;
  }
  set_status_text( object, text );
  return 0;
}

void
morphosui_statusbar_machine( const char *name )
{
  set_status_text( status_machine, name ? name : "" );
}

int
morphosui_statusbar_speed( float speed )
{
  char buffer[24];
  snprintf( buffer, sizeof( buffer ), "%3.0f%%", speed );
  set_status_text( status_speed, buffer );
  return 0;
}

static void
morphosui_apply_fullscreen_window_attrs( void )
{
#ifdef __MORPHOS__
  /* Borderless/geometry are initializer-only MUI Window attributes.
     fullscreen_win is therefore created permanently with the fullscreen
     shape; only its target Screen is changed before it is opened. */
  if( !fullscreen_win || !fullscreen_screen ) return;
  SetAttrs( (Object*)fullscreen_win,
            MUIA_Window_Screen, (IPTR)fullscreen_screen,
            TAG_DONE );
#endif
}

static void
morphosui_apply_windowed_window_attrs( void )
{
#ifdef __MORPHOS__
  if( !win ) return;
  /* The ordinary WindowObject was created with its normal borders and ID.
     Do not try to rewrite initializer-only geometry attributes here. */
  SetAttrs( (Object*)win, MUIA_Window_Screen, (IPTR)NULL, TAG_DONE );
#endif
}

static int
morphosui_rebuild_fullscreen_screen( void )
{
#ifdef __MORPHOS__
  ULONG open = FALSE;

  if( !win || !fullscreen_win || !fullscreen_native ||
      !settings_current.full_screen ) return 1;

  MOSDBG( "fullscreen rebuild begin requested=%ld logical=%lux%lu",
          (LONG)morphosui_fullscreen_video_mode(),
          (ULONG)logical_width, (ULONG)logical_height );

  if( window_open ) {
    SetAttrs( (Object*)fullscreen_win, MUIA_Window_Open, FALSE, TAG_DONE );
    window_open = 0;
  }
  if( video_attached || native_window ) {
    morphosvideo_detach();
    video_attached = 0;
    native_window = NULL;
  }

  SetAttrs( (Object*)fullscreen_win, MUIA_Window_Screen, (IPTR)NULL, TAG_DONE );
  morphosui_close_fullscreen_screen();

  if( !morphosui_open_fullscreen_screen() ) goto fail_windowed;
  morphosui_apply_fullscreen_window_attrs();
  area = fullscreen_area;
  morphosvideo_set_fullscreen( 1 );

  SetAttrs( (Object*)fullscreen_win, MUIA_Window_Open, TRUE, TAG_DONE );
  GetAttr( MUIA_Window_Open, (Object*)fullscreen_win, &open );
  window_open = open ? 1 : 0;
  if( !window_open || !morphosui_native_window() ) {
    MOSDBG( "fullscreen rebuild MUI reopen FAILED" );
    goto fail_windowed;
  }

  draw_video();
  MOSDBG( "fullscreen rebuild done screen=%08lx %lux%lu native=%ldx%ld area=%lux%lu",
          MOSPTR( fullscreen_screen ),
          (ULONG)fullscreen_screen_width, (ULONG)fullscreen_screen_height,
          (LONG)native_window->Width, (LONG)native_window->Height,
          (ULONG)_mwidth( (Object*)fullscreen_area ),
          (ULONG)_mheight( (Object*)fullscreen_area ) );
  return 0;

fail_windowed:
  if( window_open ) {
    SetAttrs( (Object*)fullscreen_win, MUIA_Window_Open, FALSE, TAG_DONE );
    window_open = 0;
  }
  if( video_attached || native_window ) {
    morphosvideo_detach();
    video_attached = 0;
    native_window = NULL;
  }
  SetAttrs( (Object*)fullscreen_win, MUIA_Window_Screen, (IPTR)NULL, TAG_DONE );
  morphosui_close_fullscreen_screen();
  settings_current.full_screen = 0;
  fullscreen_native = 0;
  morphosvideo_set_fullscreen( 0 );
  area = window_area;
  if( control_group )
    SetAttrs( (Object*)control_group, MUIA_ShowMe, TRUE, TAG_DONE );
  if( status_group )
    SetAttrs( (Object*)status_group, MUIA_ShowMe,
              settings_current.statusbar ? TRUE : FALSE, TAG_DONE );
  morphosui_apply_windowed_window_attrs();
  SetAttrs( (Object*)win, MUIA_Window_Open, TRUE, TAG_DONE );
  GetAttr( MUIA_Window_Open, (Object*)win, &open );
  window_open = open ? 1 : 0;
  if( fullscreen_item ) SetAttrs( (Object*)fullscreen_item,
    MUIA_Menuitem_Checked, FALSE, TAG_DONE );
  draw_video();
  return 1;
#else
  return 1;
#endif
}

int
morphosui_set_fullscreen( int enabled )
{
#ifdef __MORPHOS__
  struct Window *w = morphosui_native_window();
  ULONG open = FALSE;

  if( !win || !fullscreen_win ) return 1;

  enabled = !!enabled;
  last_video_click_valid = 0;
  if( enabled == fullscreen_native ) {
    if( enabled && morphosui_fullscreen_screen_needs_rebuild() )
      return morphosui_rebuild_fullscreen_screen();
    if( fullscreen_item ) SetAttrs( (Object*)fullscreen_item,
      MUIA_Menuitem_Checked, enabled ? TRUE : FALSE, TAG_DONE );
    return 0;
  }

  if( !w || !w->WScreen ) {
    settings_current.full_screen = fullscreen_native;
    return 1;
  }

  if( enabled ) {
    windowed_left = w->LeftEdge;
    windowed_top = w->TopEdge;
    windowed_width = w->Width;
    windowed_height = w->Height;

    /* Close the normal, ID-bearing MUI window. Its remembered geometry is
       deliberately kept separate from fullscreen. */
    SetAttrs( (Object*)win, MUIA_Window_Open, FALSE, TAG_DONE );
    window_open = 0;
    if( video_attached || native_window ) {
      morphosvideo_detach();
      video_attached = 0;
      native_window = NULL;
    }

    if( !morphosui_open_fullscreen_screen() ) {
      settings_current.full_screen = 0;
      fullscreen_native = 0;
      morphosvideo_set_fullscreen( 0 );
      area = window_area;
      SetAttrs( (Object*)win, MUIA_Window_Open, TRUE, TAG_DONE );
      GetAttr( MUIA_Window_Open, (Object*)win, &open );
      window_open = open ? 1 : 0;
      if( fullscreen_item ) SetAttrs( (Object*)fullscreen_item,
        MUIA_Menuitem_Checked, FALSE, TAG_DONE );
      return 1;
    }

    settings_current.full_screen = 1;
    fullscreen_native = 1;
    area = fullscreen_area;
    morphosvideo_set_fullscreen( 1 );
    morphosui_apply_fullscreen_window_attrs();

    SetAttrs( (Object*)fullscreen_win, MUIA_Window_Open, TRUE, TAG_DONE );
    GetAttr( MUIA_Window_Open, (Object*)fullscreen_win, &open );
    window_open = open ? 1 : 0;
    if( !window_open || !morphosui_native_window() )
      return morphosui_rebuild_fullscreen_screen();

    MOSDBG( "fullscreen window opened screen=%lux%lu win=%ldx%ld area=%lux%lu",
            (ULONG)fullscreen_screen->Width, (ULONG)fullscreen_screen->Height,
            (LONG)native_window->Width, (LONG)native_window->Height,
            (ULONG)_mwidth( (Object*)fullscreen_area ),
            (ULONG)_mheight( (Object*)fullscreen_area ) );
  } else {
    SetAttrs( (Object*)fullscreen_win, MUIA_Window_Open, FALSE, TAG_DONE );
    window_open = 0;
    if( video_attached || native_window ) {
      morphosvideo_detach();
      video_attached = 0;
      native_window = NULL;
    }
    SetAttrs( (Object*)fullscreen_win, MUIA_Window_Screen, (IPTR)NULL, TAG_DONE );
    morphosui_close_fullscreen_screen();

    settings_current.full_screen = 0;
    fullscreen_native = 0;
    area = window_area;
    morphosvideo_set_fullscreen( 0 );
    morphosui_apply_windowed_window_attrs();

    SetAttrs( (Object*)win, MUIA_Window_Open, TRUE, TAG_DONE );
    GetAttr( MUIA_Window_Open, (Object*)win, &open );
    window_open = open ? 1 : 0;
    if( !window_open || !morphosui_native_window() ) return 1;
  }

  if( fullscreen_item ) SetAttrs( (Object*)fullscreen_item,
    MUIA_Menuitem_Checked, enabled ? TRUE : FALSE, TAG_DONE );
  draw_video();
  return 0;
#else
  (void)enabled;
  return 1;
#endif
}

int
morphosui_set_video_size( unsigned width, unsigned height )
{
  logical_width = width ? width : 320;
  logical_height = height ? height : 240;
  MOSDBG( "set_video_size %lux%lu window_open=%ld win=%08lx mode=%s",
          (ULONG)logical_width, (ULONG)logical_height, (LONG)window_open,
          MOSPTR( win ), morphosvideo_active_mode_name() );
  morphosvideo_configure( logical_width, logical_height, logical_width * 2 );
#ifdef __MORPHOS__
  if( win && window_open &&
      morphosvideo_requested_mode() == MORPHOS_VIDEO_SURFACE &&
      !settings_current.full_screen ) {
    ULONG open = FALSE;
    MOSDBG( "set_video_size Surface fixed resize -> reopen %lux%lu",
            (ULONG)logical_width, (ULONG)logical_height );
    SetAttrs( (Object*)win, MUIA_Window_Open, FALSE, TAG_DONE );
    window_open = 0;
    if( video_attached || native_window ) {
      morphosvideo_detach();
      video_attached = 0;
      native_window = NULL;
    }
    update_window_resize_policy();
    SetAttrs( (Object*)win, MUIA_Window_Open, TRUE, TAG_DONE );
    GetAttr( MUIA_Window_Open, (Object*)win, &open );
    window_open = open ? 1 : 0;
    MOSDBG( "set_video_size Surface reopen done open=%lu native=%08lx",
            open, MOSPTR( native_window ) );
    if( !window_open || !morphosui_native_window() ) return 1;
  } else if( win && !window_open ) {
    ULONG open = FALSE;
    update_window_resize_policy();
    MOSDBG( "set_video_size opening MUI window" );
    SetAttrs( (Object*)win, MUIA_Window_Open, TRUE, TAG_DONE );
    MOSDBG( "set_video_size SetAttrs(Open=TRUE) returned native=%08lx", MOSPTR( native_window ) );
    GetAttr( MUIA_Window_Open, (Object*)win, &open );
    MOSDBG( "set_video_size GetAttr(Open)=%lu native=%08lx", open, MOSPTR( native_window ) );
    window_open = open ? 1 : 0;
    if( !window_open || !morphosui_native_window() ) {
      ui_error( UI_ERROR_ERROR, "MorphOS MUI window did not open correctly" );
      return 1;
    }
    if( settings_current.full_screen ) morphosui_set_fullscreen( 1 );
  }
  /* Surface fullscreen tracks the software scaler/filter output size.
     Recreate the private screen after logical_width/logical_height changed. */
  if( settings_current.full_screen && fullscreen_native &&
      morphosui_fullscreen_screen_needs_rebuild() )
    return morphosui_rebuild_fullscreen_screen();
#endif
  return 0;
}

int
morphosui_present( const libspectrum_word *pixels, unsigned width,
                   unsigned height, unsigned pitch )
{
  if( ui_shutting_down ) return 0;
  if( morphos_draw_trace_count < 12 )
    MOSDBG( "morphosui_present pixels=%08lx %lux%lu pitch=%lu",
            MOSPTR( pixels ), (ULONG)width, (ULONG)height, (ULONG)pitch );
  last_pixels = pixels;
  last_width = width;
  last_height = height;
  last_pitch = pitch;
  if( machine_current && machine_current->machine != status_machine_type ) {
    status_machine_type = machine_current->machine;
    morphosui_statusbar_machine( libspectrum_machine_name( status_machine_type ) );
    update_machine_menu_checks();
  }
  if( !morphosvideo_configure( width, height, pitch ) ) return 1;

  /* Match openMSX MorphOS: the normal emulation frame path only presents.
     Geometry belongs to MUIM_Draw/IDCMP_NEWSIZE.  Routing every frame through
     draw_video() caused SetVLayerAttrTags() to run every frame in Overlay and
     mixed resize/context work into TinyGL presentation. */
  if( video_attached && native_window ) {
    morphos_video_mode before = morphosvideo_active_mode();
    if( !morphosvideo_present( last_pixels, last_pitch ) ) return 1;
    if( morphosvideo_active_mode() != before ) update_video_menu_checks();
    return 0;
  }
  return draw_video();
}

void
morphosui_redraw( void )
{
  draw_video();
}

void
morphosui_set_title_speed( float speed )
{
  /* Speed is displayed in the native status bar. Keep the Window title
     stable, as in the rest of the MorphOS host UI. */
  (void)speed;
}

void
morphosui_dialog_begin( void )
{
#ifdef __MORPHOS__
  if( requester_depth++ != 0 ) return;
  requester_overlay_switched = 0;
  requester_restore_mode = morphosvideo_requested_mode();
  if( morphosvideo_active_mode() == MORPHOS_VIDEO_OVERLAY ) {
    if( morphosvideo_set_mode( MORPHOS_VIDEO_SURFACE ) ) {
      requester_overlay_switched = 1;
      update_video_option_checks();
      draw_video();
    }
  }
#endif
}

void
morphosui_dialog_end( void )
{
#ifdef __MORPHOS__
  if( requester_depth <= 0 ) { requester_depth = 0; return; }
  if( --requester_depth != 0 ) return;
  if( ui_shutting_down ) {
    requester_overlay_switched = 0;
    return;
  }
  if( requester_overlay_switched ) {
    MOSDBG( "dialog_end restoring video mode=%ld", (LONG)requester_restore_mode );
    reopen_window_for_video_mode( requester_restore_mode );
    requester_overlay_switched = 0;
    update_video_option_checks();
  }
#endif
}

int
morphosui_error_request( ui_error_level severity, const char *message )
{
#ifdef __MORPHOS__
  int guarded = 0;

  if( severity != UI_ERROR_ERROR || !MUIMasterBase ) return 0;

  if( app && win && window_open ) {
    morphosui_dialog_begin();
    guarded = 1;
  }

  /* MUI_Request accepts a NULL application and then falls back to a normal
     system requester, which also makes this useful for late startup errors
     before ApplicationObject/WindowObject are fully available. */
  MUI_Request( (Object*)app,
               (Object*)( app && win && window_open ? win : NULL ), 0,
               (STRPTR)"Fuse - Error", (STRPTR)"_OK",
               (STRPTR)"%s", (STRPTR)( message ? message : "Error" ) );

  if( guarded ) morphosui_dialog_end();
  return 1;
#else
  (void)severity; (void)message;
  return 0;
#endif
}

char *
morphosui_request_file( const char *title, int save )
{
#ifdef __MORPHOS__
  struct FileRequester *req;
  struct Window *parent = morphosui_native_window();
  char path[1024];

  morphosui_dialog_begin();
  req = (struct FileRequester*)MUI_AllocAslRequestTags(
      ASL_FileRequest,
      ASLFR_TitleText, (IPTR)( title ? title : "Fuse" ),
      ASLFR_Window, (IPTR)parent,
      ASLFR_InitialDrawer, (IPTR)requester_drawer,
      ASLFR_DoSaveMode, save ? TRUE : FALSE,
      TAG_DONE );
  if( !req ) { morphosui_dialog_end(); return NULL; }

  path[0] = 0;
  if( MUI_AslRequestTags( req, TAG_DONE ) ) {
    if( req->fr_Drawer && *req->fr_Drawer ) {
      snprintf( requester_drawer, sizeof( requester_drawer ), "%s", req->fr_Drawer );
      snprintf( path, sizeof( path ), "%s", req->fr_Drawer );
    }
    if( req->fr_File && *req->fr_File ) {
      size_t n = strlen( path );
      if( n && path[n-1] != ':' && path[n-1] != '/' )
        strncat( path, "/", sizeof( path ) - strlen( path ) - 1 );
      strncat( path, req->fr_File, sizeof( path ) - strlen( path ) - 1 );
    }
  }
  MUI_FreeAslRequest( req );
  morphosui_dialog_end();
  return path[0] ? utils_safe_strdup( path ) : NULL;
#else
  (void)title; (void)save;
  return NULL;
#endif
}

int
ui_init( int *argc, char ***argv )
{
#ifdef __MORPHOS__
  struct MUI_CustomClass *cc;
  Object *menu = NULL;
  Object *a = NULL;
  Object *fa = NULL;

  (void)argc; (void)argv;

  ui_shutting_down = 0;
  MOSDBG( "ui_init begin" );
  GfxBase = (struct GfxBase*)OpenLibrary( "graphics.library", 39 );
  IntuitionBase = (struct IntuitionBase*)OpenLibrary( "intuition.library", 39 );
  CyberGfxBase = OpenLibrary( "cybergraphics.library", 41 );
  MUIMasterBase = OpenLibrary( "muimaster.library", 20 );
  MOSDBG( "ui_init libs gfx=%08lx intuition=%08lx cyber=%08lx mui=%08lx",
          MOSPTR( GfxBase ), MOSPTR( IntuitionBase ), MOSPTR( CyberGfxBase ), MOSPTR( MUIMasterBase ) );
  if( !GfxBase || !IntuitionBase || !CyberGfxBase || !MUIMasterBase ) {
    ui_error( UI_ERROR_ERROR, "Could not open MorphOS GUI libraries" );
    goto fail;
  }

  locale_init();
  snprintf( window_title, sizeof( window_title ), "%s", "Fuse" );
  MOSDBG( "ui_init morphosvideo_init begin" );
  morphosvideo_init();
  MOSDBG( "ui_init morphosvideo_init done" );
  load_video_preferences();
  MOSDBG( "ui_init video prefs requested=%ld", (LONG)morphosvideo_requested_mode() );

  MOSDBG( "ui_init ui_widget_init begin" );
  if( ui_widget_init() ) goto fail;
  MOSDBG( "ui_init ui_widget_init done" );
  widget_initialised = 1;

  cc = MUI_CreateCustomClass( NULL, (char*)MUIC_Area, NULL,
                              sizeof( AreaData ), (APTR)&area_gate );
  if( !cc ) goto fail;
  area_class = cc;
  MOSDBG( "ui_init area class=%08lx", MOSPTR( cc ) );

  a = (Object*)NewObject( cc->mcc_Class, NULL,
                          /* The emulator canvas should meet the MUI window
                             client edges. Do not inherit user-configured
                             Area frame/inner padding here. */
                          MUIA_Frame, MUIV_Frame_None,
                          MUIA_InnerLeft, 0,
                          MUIA_InnerRight, 0,
                          MUIA_InnerTop, 0,
                          MUIA_InnerBottom, 0,
                          MUIA_Background, MUII_BACKGROUND,
                          MUIA_FillArea, TRUE,
                          TAG_DONE );
  if( !a ) goto fail;
  window_area = a;
  area = a;

  /* Fullscreen owns a separate Area because one MUI child object cannot be
     attached to two different Window roots. */
  fa = (Object*)NewObject( cc->mcc_Class, NULL,
                           MUIA_Frame, MUIV_Frame_None,
                           MUIA_InnerLeft, 0,
                           MUIA_InnerRight, 0,
                           MUIA_InnerTop, 0,
                           MUIA_InnerBottom, 0,
                           MUIA_Background, MUII_BACKGROUND,
                           MUIA_FillArea, TRUE,
                           TAG_DONE );
  if( !fa ) goto fail;
  fullscreen_area = fa;
  MOSDBG( "ui_init areas window=%08lx fullscreen=%08lx",
          MOSPTR( a ), MOSPTR( fa ) );

  menu = create_menu();
  if( !menu ) goto fail;
  MOSDBG( "ui_init menu=%08lx", MOSPTR( menu ) );

  control_reset = make_text_button( LOCSTR( MSG_006 ), LOCSTR( MSG_076 ) );
  control_pause = make_image_button( MUII_TapePause,
                                     LOCSTR( MSG_077 ), 0 );
  control_fast = make_image_button( MUII_TapePlay,
                                    LOCSTR( MSG_083 ), 1 );
  control_tape_play = make_image_button( MUII_TapePlay,
                                         LOCSTR( MSG_078 ), 0 );
  control_tape_stop = make_image_button( MUII_TapeStop, LOCSTR( MSG_079 ), 0 );
  control_tape_rewind = make_image_button( MUII_TapePlayBack,
                                           LOCSTR( MSG_080 ), 0 );
  control_disk_a_eject = make_image_button( MUII_TapeUp,
                                            LOCSTR( MSG_081 ), 0 );
  control_disk_b_eject = make_image_button( MUII_TapeUp,
                                            LOCSTR( MSG_082 ), 0 );
  if( !control_reset || !control_pause || !control_fast ||
      !control_tape_play || !control_tape_stop || !control_tape_rewind ||
      !control_disk_a_eject || !control_disk_b_eject ) goto fail;

  control_group = HGroup,
                    MUIA_Group_Spacing, 3,
                    Child, (IPTR)control_reset,
                    Child, (IPTR)control_pause,
                    Child, (IPTR)control_fast,
                    Child, (IPTR)HSpace( 4 ),
                    Child, (IPTR)control_tape_play,
                    Child, (IPTR)control_tape_stop,
                    Child, (IPTR)control_tape_rewind,
                    Child, (IPTR)HSpace( 4 ),
                    Child, (IPTR)control_disk_a_eject,
                    Child, (IPTR)control_disk_b_eject,
                    Child, (IPTR)HSpace( 0 ),
                  End;
  if( !control_group ) goto fail;

  notify_button( (Object*)control_reset, menu_machine_reset, 0 );
  notify_button( (Object*)control_pause, menu_machine_pause, 0 );
  notify_button( (Object*)control_fast, toolbar_toggle_fast, 0 );
  notify_button( (Object*)control_tape_play, menu_media_tape_play, 0 );
  notify_button( (Object*)control_tape_stop, toolbar_tape_stop, 0 );
  notify_button( (Object*)control_tape_rewind, menu_media_tape_rewind, 0 );
  notify_button( (Object*)control_disk_a_eject, menu_media_eject, 0x01 );
  notify_button( (Object*)control_disk_b_eject, menu_media_eject, 0x02 );

  status_machine = TextObject, MUIA_Text_Contents,
                         (IPTR)( machine_current ? libspectrum_machine_name( machine_current->machine ) : "" ),
                         MUIA_Text_SetMin, FALSE, End;
  status_speed = TextObject, MUIA_Text_Contents, (IPTR)"100%", MUIA_Text_SetMin, FALSE, End;
  status_tape  = TextObject, MUIA_Text_Contents, (IPTR)LOCSTR( MSG_070 ), MUIA_Text_SetMin, FALSE, End;
  status_disk  = TextObject, MUIA_Text_Contents, (IPTR)LOCSTR( MSG_072 ), MUIA_Text_SetMin, FALSE, End;
  status_mdr   = TextObject, MUIA_Text_Contents, (IPTR)LOCSTR( MSG_074 ), MUIA_Text_SetMin, FALSE, End;
  status_mouse = TextObject, MUIA_Text_Contents, (IPTR)LOCSTR( MSG_068 ), MUIA_Text_SetMin, FALSE, End;
  status_pause = TextObject, MUIA_Text_Contents, (IPTR)LOCSTR( MSG_066 ), MUIA_Text_SetMin, FALSE, End;
  if( !status_machine || !status_speed || !status_tape || !status_disk || !status_mdr ||
      !status_mouse || !status_pause ) goto fail;
  if( machine_current ) status_machine_type = machine_current->machine;
  status_group = HGroup,
                   MUIA_Group_Spacing, 6,
                   Child, (IPTR)status_machine,
                   Child, (IPTR)HSpace( 0 ),
                   Child, (IPTR)status_speed,
                   Child, (IPTR)status_tape,
                   Child, (IPTR)status_disk,
                   Child, (IPTR)status_mdr,
                   Child, (IPTR)status_mouse,
                   Child, (IPTR)status_pause,
                 End;
  if( !status_group ) goto fail;
  SetAttrs( (Object*)status_group, MUIA_ShowMe,
            settings_current.statusbar ? TRUE : FALSE, TAG_DONE );

  win = WindowObject,
          MUIA_Window_Title, (IPTR)window_title,
          MUIA_Window_ID, MAKE_ID('F','U','S','E'),
          MUIA_Window_AppWindow, TRUE,
          MUIA_Window_SizeGadget,
            morphosvideo_requested_mode() == MORPHOS_VIDEO_SURFACE ? FALSE : TRUE,
          MUIA_Window_Menustrip, (IPTR)menu,
          /* The root itself must not add horizontal padding around the
             emulator canvas.  The toolbar and status bar are HGroups and
             keep their own normal MUI inner margins. */
          MUIA_Window_RootObject, (IPTR)VGroup,
            MUIA_InnerLeft, 0,
            MUIA_InnerRight, 0,
            MUIA_InnerTop, 0,
            MUIA_InnerBottom, 0,
            Child, (IPTR)control_group,
            Child, (IPTR)a,
            Child, (IPTR)status_group,
          End,
        End;
  if( !win ) goto fail;
  MOSDBG( "ui_init window object=%08lx", MOSPTR( win ) );

  /* MUI Window geometry/border attributes are initializer-only.  A separate
     WindowObject is therefore required for a true borderless 100% fullscreen
     window.  It intentionally has no MUIA_Window_ID, so MUI cannot restore a
     remembered windowed size over the requested fullscreen geometry. */
  fullscreen_win = WindowObject,
          MUIA_Window_Title, (IPTR)window_title,
          MUIA_Window_Borderless, TRUE,
          MUIA_Window_CloseGadget, FALSE,
          MUIA_Window_SizeGadget, FALSE,
          MUIA_Window_DepthGadget, FALSE,
          MUIA_Window_DragBar, FALSE,
          MUIA_Window_LeftEdge, 0,
          MUIA_Window_TopEdge, 0,
          MUIA_Window_Width, MUIV_Window_Width_Screen( 100 ),
          MUIA_Window_Height, MUIV_Window_Height_Screen( 100 ),
          MUIA_Window_RootObject, (IPTR)VGroup,
            MUIA_InnerLeft, 0,
            MUIA_InnerRight, 0,
            MUIA_InnerTop, 0,
            MUIA_InnerBottom, 0,
            MUIA_Group_Spacing, 0,
            Child, (IPTR)fa,
          End,
        End;
  if( !fullscreen_win ) goto fail;
  MOSDBG( "ui_init fullscreen window object=%08lx", MOSPTR( fullscreen_win ) );

  app = ApplicationObject,
          MUIA_Application_Title, (IPTR)MORPHOS_APP_TITLE,
          MUIA_Application_Copyright, (IPTR)MORPHOS_APP_COPYRIGHT,
          MUIA_Application_Author, (IPTR)MORPHOS_APP_AUTHOR,
          MUIA_Application_Version, (IPTR)MORPHOS_APP_VERSION,
          MUIA_Application_Base, (IPTR)MORPHOS_APP_BASE,
          MUIA_Application_Description, (IPTR)MORPHOS_APP_DESCRIPTION,
          MUIA_Application_UseRexx, TRUE,
          MUIA_Application_Commands, (IPTR)fuse_arexx_commands,
          SubWindow, (IPTR)win,
          SubWindow, (IPTR)fullscreen_win,
        End;
  if( !app ) goto fail;
  MOSDBG( "ui_init application object=%08lx", MOSPTR( app ) );

  DoMethod( (Object*)win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_QUIT );
  DoMethod( (Object*)win, MUIM_Notify, MUIA_AppMessage, MUIV_EveryTime,
            MUIV_Notify_Application, 2, MUIM_Application_ReturnID, RID_APPDROP );

  ui_mouse_present = 1;
  MOSDBG( "ui_init OK" );
  return 0;

fail:
  MOSDBG( "ui_init FAIL app=%08lx win=%08lx fswin=%08lx area=%08lx fsarea=%08lx class=%08lx",
          MOSPTR( app ), MOSPTR( win ), MOSPTR( fullscreen_win ),
          MOSPTR( area ), MOSPTR( fullscreen_area ), MOSPTR( area_class ) );
  if( app ) {
    native_window = NULL;
    MUI_DisposeObject( (Object*)app );
    app = win = fullscreen_win = area = window_area = fullscreen_area = NULL;
  } else if( win || fullscreen_win ) {
    native_window = NULL;
    if( fullscreen_win )
      MUI_DisposeObject( (Object*)fullscreen_win );
    else if( fullscreen_area )
      MUI_DisposeObject( (Object*)fullscreen_area );
    if( win )
      MUI_DisposeObject( (Object*)win );
    else if( window_area )
      MUI_DisposeObject( (Object*)window_area );
    win = fullscreen_win = area = window_area = fullscreen_area = NULL;
  } else {
    if( status_group ) {
      MUI_DisposeObject( (Object*)status_group );
      status_group = NULL;
    } else {
      if( status_machine ) MUI_DisposeObject( (Object*)status_machine );
      if( status_speed ) MUI_DisposeObject( (Object*)status_speed );
      if( status_tape ) MUI_DisposeObject( (Object*)status_tape );
      if( status_disk ) MUI_DisposeObject( (Object*)status_disk );
      if( status_mdr ) MUI_DisposeObject( (Object*)status_mdr );
      if( status_mouse ) MUI_DisposeObject( (Object*)status_mouse );
      if( status_pause ) MUI_DisposeObject( (Object*)status_pause );
    }
    if( control_group ) {
      MUI_DisposeObject( (Object*)control_group );
      control_group = NULL;
    } else {
      if( control_reset ) MUI_DisposeObject( (Object*)control_reset );
      if( control_pause ) MUI_DisposeObject( (Object*)control_pause );
      if( control_fast ) MUI_DisposeObject( (Object*)control_fast );
      if( control_tape_play ) MUI_DisposeObject( (Object*)control_tape_play );
      if( control_tape_stop ) MUI_DisposeObject( (Object*)control_tape_stop );
      if( control_tape_rewind ) MUI_DisposeObject( (Object*)control_tape_rewind );
      if( control_disk_a_eject ) MUI_DisposeObject( (Object*)control_disk_a_eject );
      if( control_disk_b_eject ) MUI_DisposeObject( (Object*)control_disk_b_eject );
    }
    if( menu ) MUI_DisposeObject( menu );
    if( fullscreen_area ) {
      MUI_DisposeObject( (Object*)fullscreen_area );
      fullscreen_area = NULL;
    }
    if( window_area ) {
      MUI_DisposeObject( (Object*)window_area );
      window_area = NULL;
    }
    area = NULL;
  }
  if( area_class ) {
    MUI_DeleteCustomClass( (struct MUI_CustomClass*)area_class );
    area_class = NULL;
  }
  if( widget_initialised ) { ui_widget_end(); widget_initialised = 0; }
  morphosvideo_end();
  locale_cleanup();
  if( MUIMasterBase ) CloseLibrary( MUIMasterBase );
  if( CyberGfxBase ) CloseLibrary( CyberGfxBase );
  if( IntuitionBase ) CloseLibrary( (struct Library*)IntuitionBase );
  if( GfxBase ) CloseLibrary( (struct Library*)GfxBase );
  MUIMasterBase = CyberGfxBase = NULL; IntuitionBase = NULL; GfxBase = NULL;
  return 1;
#else
  (void)argc; (void)argv;
  return 1;
#endif
}

void
morphosui_widgets_reset( void )
{
#ifdef __MORPHOS__
  if( poke_window ) refresh_native_pokefinder();
#endif
}

int
ui_event( void )
{
#ifdef __MORPHOS__
  ULONG signals = 0;
  ULONG id;

  if( !app ) return 0;
  if( status_group ) SetAttrs( (Object*)status_group, MUIA_ShowMe,
                               settings_current.statusbar ? TRUE : FALSE,
                               TAG_DONE );
  update_control_state();
  for( ;; ) {
    id = DoMethod( (Object*)app, MUIM_Application_NewInput, &signals );
    if( id == 0 ) break;
    if( morphos_event_trace_count < 64 ) {
      MOSDBG( "ui_event return id=%08lx signals=%08lx", id, signals );
      morphos_event_trace_count++;
    }
    /* MUI may use its generic Quit return ID for Escape.  Never treat that
       as application exit: if a Fuse widget/menu is active, Escape closes
       that widget; otherwise Escape remains available to the emulated machine. */
    if( id == (ULONG)MUIV_Application_ReturnID_Quit ) {
      if( ui_widget_level >= 0 ) ui_widget_keyhandler( INPUT_KEY_Escape );
      continue;
    }
    if( handle_poke_return_id( id ) ) continue;
    if( handle_fixed_return_id( id ) ) continue;
    if( id >= RID_MENU_BASE && id < RID_MENU_BASE + binding_count ) {
      morphos_menu_binding *b = &bindings[id - RID_MENU_BASE];
      if( b->callback ) b->callback( b->action );
    }
  }
#endif
  return 0;
}

int
ui_end( void )
{
#ifdef __MORPHOS__
  MOSDBG( "ui_end begin app=%08lx win=%08lx fswin=%08lx native=%08lx open=%ld fullscreen=%ld",
          MOSPTR( app ), MOSPTR( win ), MOSPTR( fullscreen_win ),
          MOSPTR( native_window ), (LONG)window_open, (LONG)fullscreen_native );

  /* MUI may still send MUIM_Hide/MUIM_Draw while Window_Open is being
     cleared.  Block all new presentation first and detach the native
     presenter while the Window and framebuffer are still valid. */
  ui_shutting_down = 1;
  MOSDBG( "ui_end shutdown guard set" );

  close_native_pokefinder();
  morphoskeyboard_release_all();

  last_pixels = NULL;
  last_width = last_height = last_pitch = 0;
  if( video_attached ) {
    MOSDBG( "ui_end video detach before window close" );
    morphosvideo_detach();
    video_attached = 0;
  }

  if( window_open ) {
    Object *active = (Object*)( fullscreen_native ? fullscreen_win : win );
    if( active ) {
      MOSDBG( "ui_end window close begin fullscreen=%ld", (LONG)fullscreen_native );
      SetAttrs( active, MUIA_Window_Open, FALSE, TAG_DONE );
      MOSDBG( "ui_end window close done" );
    }
  }
  window_open = 0;
  native_window = NULL;
  if( fullscreen_win )
    SetAttrs( (Object*)fullscreen_win, MUIA_Window_Screen, (IPTR)NULL, TAG_DONE );
  morphosui_close_fullscreen_screen();
  morphosvideo_set_fullscreen( 0 );

  if( app ) {
    MOSDBG( "ui_end app dispose begin" );
    MUI_DisposeObject( (Object*)app );
    MOSDBG( "ui_end app dispose done" );
  }
  app = win = fullscreen_win = area = window_area = fullscreen_area = NULL;

  if( area_class ) {
    MOSDBG( "ui_end area class delete begin" );
    MUI_DeleteCustomClass( (struct MUI_CustomClass*)area_class );
    area_class = NULL;
    MOSDBG( "ui_end area class delete done" );
  }

  /* Only after all MUI callbacks are impossible may framebuffer/presenter
     storage be released. */
  MOSDBG( "ui_end display end begin" );
  uidisplay_end();
  MOSDBG( "ui_end display end done" );
  MOSDBG( "ui_end video end begin" );
  morphosvideo_end();
  MOSDBG( "ui_end video end done" );
  if( widget_initialised ) {
    MOSDBG( "ui_end widget end begin" );
    ui_widget_end();
    widget_initialised = 0;
    MOSDBG( "ui_end widget end done" );
  }

  video_surface_item = video_overlay_item = video_tinygl_item = NULL;
  video_vsync_item = video_linear_item = video_border_item = fullscreen_item = NULL;
  memset( input_joy_port_item, 0, sizeof( input_joy_port_item ) );
  memset( scaler_menu_item, 0, sizeof( scaler_menu_item ) );
  machine_select_menu = NULL;
  memset( machine_menu_item, 0, sizeof( machine_menu_item ) );
  memset( machine_menu_type, 0, sizeof( machine_menu_type ) );
  machine_menu_count = 0;
  input_grab_mouse_item = NULL;
  control_group = control_reset = control_pause = control_fast = NULL;
  control_tape_play = control_tape_stop = control_tape_rewind = NULL;
  control_disk_a_eject = control_disk_b_eject = NULL;
  status_group = status_machine = status_speed = status_tape = status_disk = status_mdr = NULL;
  status_mouse = status_pause = NULL;
  status_machine_type = LIBSPECTRUM_MACHINE_UNKNOWN;
  requester_depth = requester_overlay_switched = 0;
  fullscreen_native = paused_native = 0;
  menu_object_count = binding_count = 0;

  locale_cleanup();

  MOSDBG( "ui_end close libraries" );
  if( MUIMasterBase ) CloseLibrary( MUIMasterBase );
  if( CyberGfxBase ) CloseLibrary( CyberGfxBase );
  if( IntuitionBase ) CloseLibrary( (struct Library*)IntuitionBase );
  if( GfxBase ) CloseLibrary( (struct Library*)GfxBase );
  MUIMasterBase = CyberGfxBase = NULL;
  IntuitionBase = NULL;
  GfxBase = NULL;
  MOSDBG( "ui_end done" );
#endif
  return 0;
}

int
ui_mouse_grab( int startup )
{
#ifdef __MORPHOS__
  struct Window *w;
  if( startup ) return 0;
  mouse_grabbed_native = 1;
  mouse_valid = 0;
  ui_statusbar_update( UI_STATUSBAR_ITEM_MOUSE, UI_STATUSBAR_STATE_ACTIVE );
  if( input_grab_mouse_item )
    SetAttrs( (Object*)input_grab_mouse_item, MUIA_Menuitem_Checked, TRUE, TAG_DONE );
  w = morphosui_native_window();
  if( w ) {
    static UWORD blank[] = { 0, 0, 0, 0 };
    SetPointer( w, blank, 1, 16, 0, 0 );
  }
  return 1;
#else
  (void)startup; return 0;
#endif
}

int
ui_mouse_release( int suspend )
{
#ifdef __MORPHOS__
  struct Window *w = morphosui_native_window();
  (void)suspend;
  mouse_grabbed_native = 0;
  mouse_valid = 0;
  ui_statusbar_update( UI_STATUSBAR_ITEM_MOUSE, UI_STATUSBAR_STATE_INACTIVE );
  if( input_grab_mouse_item )
    SetAttrs( (Object*)input_grab_mouse_item, MUIA_Menuitem_Checked, FALSE, TAG_DONE );
  if( w ) ClearPointer( w );
#else
  (void)suspend;
#endif
  return 0;
}
