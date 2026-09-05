#ifndef _FOO_BPM_COMPAT_TMSCHEMA_H_
#define _FOO_BPM_COMPAT_TMSCHEMA_H_

// The 2011 SDK's shared.h includes <tmschema.h>, a Platform SDK header that
// Microsoft dropped after the Windows 7 SDK. Its theme constants live in
// <vssym32.h> now, and TMT_MENUFONT - the only thing shared.h takes from it -
// is one of them.
//
// This shim is on the include path via scripts\external.props, ahead of the
// Windows SDK, so the SDK sources compile unmodified.

#include <vssym32.h>

#endif
