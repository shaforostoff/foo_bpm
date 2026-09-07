// Renders foo_rubato's dialog templates out of the built DLL, and checks that
// every label fits the control drawn around it.
//
// The component's dialogs cannot be exercised without foobar2000, so for a
// long time they were only ever compiled - and two layout faults reached a
// release that way: three checkboxes carrying BS_CENTER, which sets a short
// label adrift in the middle of its control instead of against the box, and
// before that a results window whose columns were sized by guesswork. Neither
// is visible in the .rc file, and both are obvious the moment the thing is
// drawn.
//
// This needs no foobar2000: a dialog template is just a resource, and the
// dialog manager will build it from any process. What it cannot show is
// anything the component fills in at run time - list columns, combo box items,
// the contents of edit controls, which check is set - so what is being checked
// here is geometry and the text baked into the template.
//
//   dialog_test <dll> [--out <dir>] [--font <face> <points>]
//
// Exit codes: 0 every label fits, 1 something is clipped, 77 no desktop to
// draw on (CTest is told to read that as a skip).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int exit_ok = 0;
const int exit_clipped = 1;
const int exit_no_desktop = 77;

HFONT g_font = nullptr;
int g_clipped = 0;
std::vector<int> g_dialog_ids;

BOOL CALLBACK collect_dialog(HMODULE, LPCSTR, LPSTR name, LONG_PTR)
{
	// Dialogs are addressed by number here; a named one would need the name
	// carried through to the report, and this component has none.
	if (IS_INTRESOURCE(name))
	{
		g_dialog_ids.push_back(static_cast<int>(reinterpret_cast<uintptr_t>(name)));
	}
	return TRUE;
}

BOOL CALLBACK apply_font(HWND child, LPARAM)
{
	SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	return TRUE;
}

INT_PTR CALLBACK stub_proc(HWND, UINT msg, WPARAM, LPARAM)
{
	return msg == WM_INITDIALOG ? TRUE : FALSE;
}

// Does the label fit the control drawn around it?
//
// This is the check worth automating. Reading a render by eye is what let the
// centred checkboxes through in the first place, and a clipped label is only
// obvious once someone happens to look at the right part of the page.
BOOL CALLBACK check_fit(HWND child, LPARAM)
{
	char cls[64] = {};
	GetClassNameA(child, cls, sizeof(cls) - 1);

	char text[1024] = {};
	if (GetWindowTextA(child, text, sizeof(text) - 1) == 0) return TRUE;

	const bool is_static = _stricmp(cls, "Static") == 0;
	const bool is_button = _stricmp(cls, "Button") == 0;
	if (!is_static && !is_button) return TRUE;

	const LONG style = GetWindowLongA(child, GWL_STYLE);

	if (is_button)
	{
		const LONG type = style & 0x0f;
		// A group box's caption sits in its top edge and is clipped by nothing;
		// a push button is sized to suit the row it is in, not to its text.
		const bool is_check = type == BS_CHECKBOX || type == BS_AUTOCHECKBOX ||
		                      type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
		if (!is_check) return TRUE;
	}
	else if ((style & SS_TYPEMASK) != SS_LEFT &&
	         (style & SS_TYPEMASK) != SS_CENTER &&
	         (style & SS_TYPEMASK) != SS_RIGHT)
	{
		// Icons, frames, owner-drawn statics: nothing with text to measure.
		return TRUE;
	}

	RECT have = {};
	GetClientRect(child, &have);

	// A plain LTEXT wraps, so a label too narrow for its text is not cut off
	// sideways - it takes a second line and is cut off below. Measuring such a
	// control as one line reports a paragraph of explanatory prose as broken
	// when it is laid out exactly as intended, so a wrapping control is checked
	// by height at its own width and everything else by width.
	const bool wraps = is_static && (style & SS_LEFTNOWORDWRAP) == 0 &&
	                   (style & SS_SIMPLE) == 0;

	HFONT font = reinterpret_cast<HFONT>(SendMessage(child, WM_GETFONT, 0, 0));
	HDC dc = GetDC(child);
	HGDIOBJ previous = font != nullptr ? SelectObject(dc, font) : nullptr;
	RECT need = { 0, 0, wraps ? have.right - have.left : 0, 0 };
	DrawTextA(dc, text, -1, &need,
	          DT_CALCRECT | DT_LEFT | (wraps ? DT_WORDBREAK : DT_SINGLELINE));
	if (previous != nullptr) SelectObject(dc, previous);
	ReleaseDC(child, dc);

	// The box itself, and the gap between box and text, on top of the text.
	const int glyph = is_button ? GetSystemMetrics(SM_CXMENUCHECK) + 6 : 0;
	const int required = wraps ? need.bottom - need.top
	                           : (need.right - need.left) + glyph;
	const int available = wraps ? have.bottom - have.top : have.right - have.left;

	if (required > available)
	{
		g_clipped++;
		// One line of the text is enough to name the control; a wrapped
		// paragraph reproduced in full would bury the report.
		char shown[73] = {};
		for (int i = 0; i < 72 && text[i] != 0; i++)
		{
			shown[i] = (text[i] == '\r' || text[i] == '\n') ? ' ' : text[i];
		}
		std::printf("    CLIPPED  id %-6d %-7s needs %4d%s, has %4d  \"%s\"\n",
		            GetDlgCtrlID(child), cls, required,
		            wraps ? "px tall" : "px wide", available, shown);
	}
	return TRUE;
}

bool save_png(const unsigned char * pixels, int w, int h, int stride,
              const std::string & path)
{
	// The PNG encoder's class id, as documented for GDI+.
	const CLSID png = { 0x557cf406, 0x1a04, 0x11d3,
	                    { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

	// Built from 24bpp pixels rather than from the HBITMAP: a bitmap made
	// compatible with the screen is 32bpp with every alpha byte left at zero,
	// and GDI+ takes that at its word and writes a fully transparent PNG.
	Gdiplus::Bitmap bitmap(w, h, stride, PixelFormat24bppRGB,
	                       const_cast<BYTE *>(pixels));
	if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;

	const std::wstring wide(path.begin(), path.end());
	return bitmap.Save(wide.c_str(), &png, nullptr) == Gdiplus::Ok;
}

//! Draw one dialog, check it, and write a PNG if asked for one.
bool render_one(HMODULE dll, int id, HINSTANCE app, const char * out_dir,
                const char * font_face, int font_points)
{
	HRSRC res = FindResourceA(dll, reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(id)),
	                          reinterpret_cast<LPCSTR>(RT_DIALOG));
	if (res == nullptr)
	{
		std::fprintf(stderr, "dialog %d disappeared between listing and loading\n", id);
		return false;
	}
	void * tmpl = LockResource(LoadResource(dll, res));

	// Every template here is WS_CHILD or a popup owned by something; either way
	// it needs a window to belong to.
	HWND host = CreateWindowExA(0, "dialog_test_host", "dialog_test", WS_POPUP,
	                            0, 0, 16, 16, nullptr, nullptr, app, nullptr);
	if (host == nullptr) return false;

	HWND dlg = CreateDialogIndirectParamA(app, static_cast<LPCDLGTEMPLATE>(tmpl),
	                                      host, stub_proc, 0);
	if (dlg == nullptr)
	{
		std::fprintf(stderr, "dialog %d would not open: %lu\n", id, GetLastError());
		DestroyWindow(host);
		return false;
	}

	// Off by default, and to be used knowing what it does. The dialog manager
	// derives dialog units from the template's own FONT, so every control
	// rectangle is already expressed in terms of that face. Pushing a wider one
	// onto controls sized from a narrower one makes four healthy labels report
	// as clipped - which happened, and cost an hour. It is here only to ask
	// what would happen if a host restyled the page.
	if (font_face != nullptr)
	{
		HDC dc = GetDC(nullptr);
		const int height = -MulDiv(font_points, GetDeviceCaps(dc, LOGPIXELSY), 72);
		ReleaseDC(nullptr, dc);
		g_font = CreateFontA(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
		SendMessage(dlg, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
		EnumChildWindows(dlg, apply_font, 0);
	}

	// A WS_CHILD template lives inside the host, so the host is what to draw.
	// A popup only has the host as its owner, and drawing the host would give a
	// blank rectangle.
	const bool child = (GetWindowLongA(dlg, GWL_STYLE) & WS_CHILD) != 0;
	HWND target = child ? host : dlg;

	RECT r = {};
	GetWindowRect(dlg, &r);
	const int w = r.right - r.left, h = r.bottom - r.top;
	if (child)
	{
		SetWindowPos(host, nullptr, 100, 100, w, h, SWP_NOZORDER);
		SetWindowPos(dlg, nullptr, 0, 0, w, h, SWP_NOZORDER);
		ShowWindow(host, SW_SHOW);
	}
	else
	{
		SetWindowPos(dlg, nullptr, 100, 100, w, h, SWP_NOZORDER);
	}
	ShowWindow(dlg, SW_SHOW);

	// Let the controls paint before anything is captured.
	for (int i = 0; i < 40; i++)
	{
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		Sleep(10);
	}

	std::printf("  dialog %d  %dx%d px%s\n", id, w, h, child ? "  (child)" : "");

	bool ok = true;
	if (out_dir != nullptr)
	{
		BITMAPINFOHEADER bi = {};
		bi.biSize = sizeof(bi);
		bi.biWidth = w;
		bi.biHeight = -h;            // top down, which is what GDI+ expects
		bi.biPlanes = 1;
		bi.biBitCount = 24;
		bi.biCompression = BI_RGB;

		const int stride = ((w * 3 + 3) / 4) * 4;
		std::vector<unsigned char> pixels(static_cast<size_t>(stride) * h);

		HDC screen = GetDC(nullptr);
		HDC mem = CreateCompatibleDC(screen);
		HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
		HGDIOBJ previous = SelectObject(mem, bmp);
		if (!PrintWindow(target, mem, 0))
		{
			std::fprintf(stderr, "  could not capture dialog %d: %lu\n", id, GetLastError());
			ok = false;
		}
		SelectObject(mem, previous);
		if (ok && GetDIBits(screen, bmp, 0, h, pixels.data(),
		                    reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS) == 0)
		{
			std::fprintf(stderr, "  could not read dialog %d back: %lu\n", id, GetLastError());
			ok = false;
		}
		DeleteObject(bmp);
		DeleteDC(mem);
		ReleaseDC(nullptr, screen);

		if (ok)
		{
			char path[MAX_PATH];
			std::snprintf(path, sizeof(path), "%s\\dialog_%d.png", out_dir, id);
			if (save_png(pixels.data(), w, h, stride, path))
			{
				std::printf("    %s\n", path);
			}
			else
			{
				std::fprintf(stderr, "    could not write %s\n", path);
				ok = false;
			}
		}
	}

	EnumChildWindows(dlg, check_fit, 0);

	if (g_font != nullptr)
	{
		DeleteObject(g_font);
		g_font = nullptr;
	}
	DestroyWindow(dlg);
	DestroyWindow(host);
	return ok;
}

}   // namespace

int main(int argc, char ** argv)
{
	const char * dll_path = nullptr;
	const char * out_dir = nullptr;
	const char * font_face = nullptr;
	int font_points = 0;

	for (int i = 1; i < argc; i++)
	{
		if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc)
		{
			out_dir = argv[++i];
		}
		else if (std::strcmp(argv[i], "--font") == 0 && i + 2 < argc)
		{
			font_face = argv[++i];
			font_points = std::atoi(argv[++i]);
		}
		else if (dll_path == nullptr)
		{
			dll_path = argv[i];
		}
		else
		{
			std::fprintf(stderr, "unexpected argument: %s\n", argv[i]);
			return 2;
		}
	}

	if (dll_path == nullptr)
	{
		std::fprintf(stderr,
		             "usage: dialog_test <dll> [--out <dir>] [--font <face> <points>]\n");
		return 2;
	}

	// A build with no desktop to draw on cannot answer the question either way,
	// and saying so is better than failing. CTest reads this as a skip.
	if (GetSystemMetrics(SM_CXSCREEN) <= 0)
	{
		std::printf("dialog_test: no desktop to draw on, skipping\n");
		return exit_no_desktop;
	}

	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);

	// LOAD_LIBRARY_AS_DATAFILE maps the file without running any of it, and
	// resources can be read across architectures - so the x64 harness can read
	// the x86 DLL and the other way round.
	HMODULE dll = LoadLibraryExA(dll_path, nullptr, LOAD_LIBRARY_AS_DATAFILE);
	if (dll == nullptr)
	{
		std::fprintf(stderr, "could not open %s: %lu\n", dll_path, GetLastError());
		return 2;
	}

	if (!EnumResourceNamesA(dll, reinterpret_cast<LPCSTR>(RT_DIALOG), collect_dialog, 0) &&
	    g_dialog_ids.empty())
	{
		std::fprintf(stderr, "no dialogs in %s\n", dll_path);
		return 2;
	}

	HINSTANCE app = GetModuleHandleA(nullptr);

	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = app;
	wc.lpszClassName = "dialog_test_host";
	wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
	if (RegisterClassA(&wc) == 0)
	{
		std::printf("dialog_test: no window station to draw on, skipping\n");
		return exit_no_desktop;
	}

	Gdiplus::GdiplusStartupInput gdip_input;
	ULONG_PTR gdip = 0;
	if (out_dir != nullptr && Gdiplus::GdiplusStartup(&gdip, &gdip_input, nullptr) != Gdiplus::Ok)
	{
		std::fprintf(stderr, "could not start GDI+\n");
		return 2;
	}

	std::printf("dialog_test: %s, %d dialog%s\n", dll_path,
	            static_cast<int>(g_dialog_ids.size()),
	            g_dialog_ids.size() == 1 ? "" : "s");

	bool failed = false;
	for (size_t i = 0; i < g_dialog_ids.size(); i++)
	{
		if (!render_one(dll, g_dialog_ids[i], app, out_dir, font_face, font_points))
		{
			failed = true;
		}
	}

	if (out_dir != nullptr) Gdiplus::GdiplusShutdown(gdip);
	FreeLibrary(dll);

	std::printf("%d label%s clipped\n", g_clipped, g_clipped == 1 ? "" : "s");
	if (failed) return 2;
	return g_clipped == 0 ? exit_ok : exit_clipped;
}
