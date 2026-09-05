#ifndef _FOO_BPM_COMPAT_AFXRES_H_
#define _FOO_BPM_COMPAT_AFXRES_H_

// foo_bpm.rc includes <afxres.h> because that is what the Visual Studio
// resource editor writes into the TEXTINCLUDE block, whether or not the
// project uses MFC. This one does not - it only needs the standard resource
// symbols and IDC_STATIC - but afxres.h ships with the optional MFC component,
// so the resource compiler fails on any Visual Studio installed without it.
//
// This shim is on the resource compiler's include path via
// scripts\external.props, so foo_bpm.rc compiles unmodified.

#include <windows.h>

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#endif
