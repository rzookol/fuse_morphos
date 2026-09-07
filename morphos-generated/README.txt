Pre-generated build inputs for the minimal MorphOS SDK path.

They let the normal Makefile.morphos build avoid a Perl dependency.  The Fuse
files were generated for the MorphOS/widget UI configuration used by the
wrapper (AHI, joystick and zlib enabled; PNG/SDL/GTK/libxml2/pthreads/sockets disabled).
The libspectrum header is prepared for bundled fake GLib with zlib enabled and
no bzip2/gcrypt/WAV backend.

These are build inputs only; upstream generator scripts remain in the source
for developers who want to regenerate them on another host.
