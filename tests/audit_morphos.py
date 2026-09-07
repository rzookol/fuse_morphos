#!/usr/bin/env python3
"""Static sanity audit for the native Fuse/MorphOS frontend.

This intentionally mirrors the role of the audit scripts in the supplied
openMSX MorphOS port. It does not replace a PPC MorphOS compile; it catches
accidental regression to SDL/GTK and missing native backend integration.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'fuse-1.9.2').resolve()
errors = []

def text(rel):
    p = root / rel
    if not p.exists():
        errors.append(f'missing {rel}')
        return ''
    return p.read_text(errors='ignore')

def require(rel, needle):
    if needle not in text(rel):
        errors.append(f'{rel}: missing {needle!r}')

def forbid(rel, pattern):
    if re.search(pattern, text(rel), re.I | re.M):
        errors.append(f'{rel}: forbidden pattern {pattern!r}')

# Native MorphOS libnix startup must get a larger PPC stack than the ~32 KiB default.
require('fuse.c', 'int __stack = 100000;')

# Build-system integration.
require('configure.ac', 'UI=morphos; WIDGET=widget')
require('configure.ac', 'AM_CONDITIONAL(UI_MORPHOS')
require('configure.ac', "SOUND_LIBADD='sound/ahisound.$(OBJEXT)'")
require('configure.ac', 'AC_DEFINE([USE_JOYSTICK], 1')
require('Makefile.am', 'include ui/morphos/Makefile.am')
for f in ['morphosdisplay.c', 'morphoskeyboard.c', 'morphosjoystick.c',
          'morphosvideo.c', 'locale.c', 'morphosui.c']:
    require('ui/morphos/Makefile.am', f)

# locale.library + CatComp catalog support with English fallback.  Keep this
# layout aligned with the RDesktopGui MorphOS port.
locale = 'ui/morphos/locale.c'
for marker in ['OpenLibrary( "locale.library"', 'OpenCatalog(', 'GetCatalogStr(',
               'Fuse.catalog', 'CatCompArray']:
    require(locale, marker)
for marker in ['CATCOMP_ARRAY', 'NEW_CATCOMP_ARRAY_IDS', 'MSG_000_ID']:
    require('ui/morphos/Fuse_strings.h', marker)
require('ui/morphos/locale.h', '#define LOCSTR(x)')
require('ui/morphos/morphosui.c', 'LOCSTR( MSG_')
for rel in ['ui/morphos/Fuse.cd', 'ui/morphos/catalogs/polski/Fuse.ct',
            'ui/morphos/Fuse_strings.h']:
    if not (root / rel).exists(): errors.append(f'missing {rel}')
require('ui/morphos/Makefile.am', 'ui/morphos/Fuse.cd')
require('ui/morphos/Makefile.am', 'ui/morphos/catalogs/polski/Fuse.ct')
require('ui/morphos/catalogs/polski/Fuse.ct', '## codeset 5')
# Polish MorphOS catalogs use ISO-8859-2 (IANA MIBenum 5), not UTF-8.
ct_path = root / 'ui/morphos/catalogs/polski/Fuse.ct'
if ct_path.exists():
    ct_bytes = ct_path.read_bytes()
    if any(b >= 0x80 for b in ct_bytes):
        try:
            ct_bytes.decode('utf-8')
        except UnicodeDecodeError:
            pass
        else:
            errors.append('ui/morphos/catalogs/polski/Fuse.ct: must be ISO-8859-2, not UTF-8')
    ct_iso2 = ct_bytes.decode('iso-8859-2')
    for marker in ['Wyjście', 'Wyczyść', 'Pełny ekran', 'Włóż...']:
        if marker not in ct_iso2:
            errors.append(f'ui/morphos/catalogs/polski/Fuse.ct: ISO-8859-2 check missing {marker!r}')
for marker in ['Project', 'Scaler', 'Fullscreen', 'Grab mouse', 'Filter options...']:
    require('ui/morphos/Fuse.cd', marker)

# Native GUI/input and no C++ in the MorphOS frontend.
require('ui/morphos/morphosui.c', 'ApplicationObject')
require('ui/morphos/morphosui.c', 'IDCMP_RAWKEY')
require('ui/morphos/morphosui.c', 'MUI_AllocAslRequestTags')
require('ui/morphos/morphosui.c', 'gamepad_titles')
require('ui/morphos/morphosui.c', 'ENV:Fuse_MorphOS_prefs')
# GTK frontend parity brought to MorphOS: AppWindow drops, host accelerators,
# machine name in the status bar and native composite filter controls.
for marker in ['MUIA_Window_AppWindow', 'MUIA_AppMessage', 'utils_open_file(',
               'RAWKEY_F2', 'RAWKEY_F11', 'RAWKEY_INSERT', 'RAWKEY_DELETE',
               'status_machine', 'requester_drawer', 'composite_filter_hue',
               'composite_filter_bleed']:
    require('ui/morphos/morphosui.c', marker)

# Native GTK-parity tools added in the extended MUI frontend.
for marker in ['show_native_tape_browser', 'show_native_memory_browser',
               'show_native_pokefinder', 'show_native_debug_monitor',
               'MUIM_List_InsertSingle', 'debugger_command_evaluate',
               'debugger_breakpoint_add_address', 'MUIA_Window_Borderless']:
    require('ui/morphos/morphosui.c', marker)
# Post-audit fixes: MUI submenu hierarchy, modeless/copy-safe poke finder and
# debugger bridge must stay intact.
for marker in ['MUIV_List_ConstructHook_String', 'handle_poke_return_id',
               'poke_overlay_guard', 'morphosui_debugger_activate',
               'morphosui_debugger_deactivate', 'if( requester_depth > 0 ) {']:
    require('ui/morphos/morphosui.c', marker)
require('ui/widget/debugger.c', 'return morphosui_debugger_activate()')
require('ui/widget/debugger.c', 'return morphosui_debugger_deactivate( interruptible )')
# Nested menu nodes must be Menuitem.mui; Menu.mui is reserved for top-level
# menustrip children.
require('ui/morphos/morphosui.c', 'if( !nested )')
require('ui/morphos/morphosui.c', 'MUI_NewObject( MUIC_Menuitem')

# openMSX-style MUI ARexx integration.
for marker in ['MUIA_Application_UseRexx', 'MUIA_Application_Commands',
               'fuse_arexx_commands', '(char*)"OPEN"', '(char*)"STATUS"']:
    require('ui/morphos/morphosui.c', marker)
# Modal safety, native toolbar and native Aboutbox are modelled after openMSX.
for marker in ['morphosui_dialog_begin', 'MORPHOS_VIDEO_SURFACE',
               'MUIA_ShortHelp', 'MUII_TapePlay', 'control_tape_play',
               'specplus3_get_fdd', 'AboutboxObject']:
    require('ui/morphos/morphosui.c', marker)
require('ui/widget/widget.c', 'morphosui_dialog_begin()')
require('ui/widget/widget.c', 'morphosui_dialog_end()')
for p in (root / 'ui/morphos').glob('*'):
    if p.suffix.lower() in {'.cc', '.cpp', '.cxx'}:
        errors.append(f'native frontend must stay C: {p.relative_to(root)}')

# Native presenters: Surface + CGXVideo Overlay + TinyGL, with accelerated
# paths falling back safely to the next renderer.
video = 'ui/morphos/morphosvideo.c'
for marker in ['WritePixelArray', 'CreateVLayerHandleTags', 'AttachVLayerTags',
               'LockVLayer', 'SwapVLayerBuffer', 'VOA_DoubleBuffer',
               'OpenLibrary( "cgxvideo.library"', 'OpenLibrary( "tinygl.library"',
               'GLAInitializeContext', 'GLTexImage2D', 'GLTexSubImage2D',
               'GLASwapBuffers', 'BMF_3DTARGET']:
    require(video, marker)
require(video, 'MORPHOS_VIDEO_OVERLAY')
require(video, 'MORPHOS_VIDEO_TINYGL')
require(video, 'MORPHOS_VIDEO_SURFACE')
require(video, 'morphosvideo_has_tinygl( void )')
# Optional G4/G5 VMX acceleration must remain runtime-dispatched: baseline
# MorphOS code stays scalar and the dedicated object carries -maltivec.
for marker in ['SYSTEMINFOTYPE_PPC_ALTIVEC', 'morphosvideo_swap_rgb565le_altivec',
               'morphosvideo_rgb565_to_argb8888_altivec',
               'morphosvideo_rgb565_to_rgba8888_altivec']:
    require(video, marker)
altivec = 'ui/morphos/morphosvideo_altivec.c'
for marker in ['<altivec.h>', 'vec_perm', 'vec_ld', 'vec_st',
               'morphosvideo_swap_rgb565le_altivec',
               'morphosvideo_rgb565_to_rgba8888_altivec']:
    require(altivec, marker)


# AHI follows the openMSX choice: system/default AHI unit, no private selector.
ahi = 'sound/ahisound.c'
require(ahi, 'AHI_DEFAULT_UNIT')
require(ahi, 'AHIST_S16S')
require(ahi, 'AHIST_M16S')
require(ahi, 'ahir_Link')
require(ahi, 'AHI_REQUEST_COUNT 2')
for marker in ['PCM_SLOT_COUNT 12', 'RECOVERY_SILENCE_FRAMES 512',
               'CLOCK_CORRECTION_PPM_PER_SLOT 3750', 'NewCreateTaskA',
               'TASKTAG_STARTUPMSG', 'ABOX_TASK( void, FuseAHITask']:
    require(ahi, marker)
# Producer path must never synchronously wait for ahi.device.
ahi_text = text(ahi)
frame = ahi_text.split('sound_lowlevel_frame(', 1)[1] if 'sound_lowlevel_frame(' in ahi_text else ''
frame = frame.split('#endif', 1)[0]
if 'WaitIO(' in frame:
    errors.append('sound/ahisound.c: producer sound_lowlevel_frame() contains blocking WaitIO()')
forbid(ahi, r'ahi:\\?%?[0-9]')

# lowlevel.library gamepad backend, hot-plug polling and CD32 fire convention.
joy = 'ui/morphos/morphosjoystick.c'
for marker in ['OpenLibrary( "lowlevel.library"', 'ReadJoyPort',
               'JPF_BUTTON_RED', 'JPF_BUTTON_BLUE', 'JPF_BUTTON_YELLOW',
               'INPUT_JOYSTICK_FIRE_1', 'INPUT_JOYSTICK_FIRE_2']:
    require(joy, marker)
require(joy, 'MORPHOS_LOWLEVEL_PORTS    4')
require(joy, 'MORPHOS_LOGICAL_JOYSTICKS 2')

# Compatibility cleanup: do not regress to the historical hard-coded OS name
# or unsafe path termination logic.
forbid('compat/morphos/osname.c', r'Pegasos|1\.4\.4')
require('compat/morphos/osname.c', '"MorphOS"')
forbid('compat/amiga/paths.c', r'buffer\[\s*PATH_MAX\s*-\s*1\s*\]')

# The native backend itself must not depend on SDL/GTK.
for rel in ['ui/morphos/morphosui.c', 'ui/morphos/morphosdisplay.c',
            'ui/morphos/morphoskeyboard.c', 'ui/morphos/morphosjoystick.c',
            'ui/morphos/morphosvideo.c', 'ui/morphos/locale.c', 'sound/ahisound.c']:
    forbid(rel, r'#\s*include\s*[<"]SDL')
    forbid(rel, r'#\s*include\s*[<"]gtk')

if errors:
    print('Fuse MorphOS audit: FAIL')
    for e in errors:
        print(' -', e)
    raise SystemExit(1)

print('Fuse MorphOS audit: PASS')
print('GUI=MUI/Intuition/ASL+toolbar/Aboutbox+locale+DnD+ARexx+GTK shortcuts/filter parity; video=Surface+CGXVideo Overlay+TinyGL; audio=AHI worker/ring+startup join; input=RAWKEY+lowlevel.library')
