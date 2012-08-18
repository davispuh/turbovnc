//  Based on LowLevelHook.cpp from Ultr@VNC, written by Assaf Gordon
//  (Assaf@mazleg.com), 10/9/2003 (original source lacks copyright attribution)
//  Modifications:
//  Copyright (C) 2012 D. R. Commander.  All Rights Reserved.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.

// This is the source for the low-level keyboard hook, which allows
// intercepting and sending special keys (such as ALT+TAB, etc.) to the VNC
// server.

#include "LowLevelHook.h"

HWND LowLevelHook::g_hwndVNCViewer = NULL;
DWORD LowLevelHook::g_VncProcessID = 0;
HHOOK LowLevelHook::g_HookID = 0;
HANDLE LowLevelHook::g_hThread = NULL;
DWORD LowLevelHook::g_nThreadID = 0;
omni_mutex LowLevelHook::g_Mutex;

void LowLevelHook::Initialize(HINSTANCE hInstance)
{
	// adzm 2009-09-25 - Install the hook in a different thread.  We receive
	// the hook callbacks via the message pump, so by using it on the main
	// connection thread, it could be delayed because of file transfers, etc.
	// Thus, we use a dedicated thread.
	g_hThread = CreateThread(NULL, 0, HookThreadProc, hInstance, 0,
		&g_nThreadID);
	if (!g_hThread)
		vnclog.Print(0, "Error %d from CreateThread()", GetLastError());
}

// adzm 2009-09-25 - Hook events handled on this thread
DWORD WINAPI LowLevelHook::HookThreadProc(LPVOID lpParam)
{
	HINSTANCE hInstance = (HINSTANCE)lpParam;

	MSG msg;
	BOOL bRet;

	// Ensure that the message queue is operational
	PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
	
    // Try to set the hook procedure
    g_HookID = SetWindowsHookEx(WH_KEYBOARD_LL, VncLowLevelKbHookProc,
		hInstance, 0);

	if (g_HookID == 0) {
		vnclog.Print(0, "Error %d from SetWindowsHookEx()", GetLastError());
		return 0;
	}

	while( (bRet = GetMessage(&msg, NULL, 0, 0)) != 0) { 
		if (bRet == -1) {
			vnclog.Print(0, "Error %d from GetMessage()", GetLastError());
			return 0;
		} else if (msg.message == WM_SHUTDOWNLLKBHOOK) {
			PostQuitMessage(0);
		} else {
			TranslateMessage(&msg); 
			DispatchMessage(&msg); 
		}
	}

	return 0;
}

void LowLevelHook::Release()
{
	// adzm 2009-09-25 - Post a message to the thread instructing it to
    // terminate
	if (g_hThread) {
		PostThreadMessage(g_nThreadID, WM_SHUTDOWNLLKBHOOK, 0, 0);
		WaitForSingleObject(g_hThread, INFINITE);
		CloseHandle(g_hThread);
	}

	if (g_HookID) UnhookWindowsHookEx(g_HookID);
}

void LowLevelHook::Activate(HWND win)
{
	omni_mutex_lock l(g_Mutex);

	g_hwndVNCViewer = win;

	// Store the process ID of the VNC window.	This will prevent the keyboard
	// hook procedure from interfering with keypresses in other processes'
	// windows.
	GetWindowThreadProcessId(g_hwndVNCViewer, &g_VncProcessID);
}

void LowLevelHook::Deactivate(void)
{
	omni_mutex_lock l(g_Mutex);
	g_hwndVNCViewer = NULL;
	g_VncProcessID = 0;
}

bool LowLevelHook::isActive(HWND win)
{
	return win == g_hwndVNCViewer;
}

LRESULT CALLBACK LowLevelHook::VncLowLevelKbHookProc(INT nCode, WPARAM wParam,
	LPARAM lParam)
{
	omni_mutex_lock l(g_Mutex);

	// If set to TRUE, the keypress message will NOT be passed on to Windows.
    BOOL fHandled = FALSE;

	BOOL fKeyDown = FALSE;

    if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT *pkbdllhook = (KBDLLHOOKSTRUCT *)lParam;

		DWORD ProcessID ;

		// Get the process ID of the Active Window (the window with the input
		// focus)
		HWND hwndCurrent = GetForegroundWindow();
		GetWindowThreadProcessId(hwndCurrent, &ProcessID);

		fKeyDown = ((wParam==WM_KEYDOWN) || (wParam==WM_SYSKEYDOWN));

		// Intercept keypresses only if this is the vncviewer process and
		// only if the foreground window is the one we want to hook
		if (ProcessID == g_VncProcessID && isActive(hwndCurrent)) {
			int xkey;

			switch (pkbdllhook->vkCode)	{
				case VK_LWIN:
					xkey = XK_Super_L;  break;
				case VK_RWIN:
					xkey = XK_Super_R;  break;
				case VK_APPS:
					xkey = XK_Menu;  break;
			}

			switch (pkbdllhook->vkCode)	{
				case VK_LWIN:
				case VK_RWIN:
				case VK_APPS:
					PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
						fKeyDown? ID_CONN_SENDKEYDOWN : ID_CONN_SENDKEYUP,
						xkey);
					fHandled = TRUE;
					break;


				// For window switching sequences (ALT+TAB, ALT+ESC, CTRL+ESC),
				// we intercept the primary keypress when it occurs after the
				// modifier keypress, then we look for the corresponding
				// primary key release and intercept it as well.  Both primary
				// keystrokes are sent to the VNC server but not to Windows.
				case VK_TAB:
				{
					static BOOL fAltTab = FALSE;
					if (pkbdllhook->flags & LLKHF_ALTDOWN && fKeyDown) {
						PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
							ID_CONN_SENDKEYDOWN, XK_Tab);
						fHandled = TRUE;
						fAltTab = TRUE;
					} else if (fAltTab) {
						if (!fKeyDown) {
							PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
								ID_CONN_SENDKEYUP, XK_Tab);
							fHandled = TRUE;
						}
						fAltTab = FALSE;
					}
					break;
				}

				case VK_ESCAPE:
				{
					static BOOL fAltEsc = FALSE, fCtrlEsc = FALSE;
					if (pkbdllhook->flags & LLKHF_ALTDOWN && fKeyDown) {
						PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
							ID_CONN_SENDKEYDOWN, XK_Escape);
						fHandled = TRUE;
						fAltEsc = TRUE;
					} else if (fAltEsc) {
						if (!fKeyDown) {
							PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
								ID_CONN_SENDKEYUP, XK_Escape);
							fHandled = TRUE;
						}
						fAltEsc = FALSE;
					}
					if ((GetAsyncKeyState(VK_CONTROL) & 0x8000)
						&& fKeyDown) {
						PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
							ID_CONN_SENDKEYDOWN, XK_Escape);
						fHandled = TRUE;
						fCtrlEsc = TRUE;
					} else if (fCtrlEsc) {
						if (!fKeyDown) {
							PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
								ID_CONN_SENDKEYUP, XK_Escape);
							fHandled = TRUE;
						}
						fCtrlEsc = FALSE;
					}
					break;
				}

			} // switch(pkbdllhook->vkCode)

		} // if (ProcessID == g_VncProcesID && isActive(hwndCurrent))

		if (ProcessID == g_VncProcessID) {

			switch (pkbdllhook->vkCode)	{
				// Hotkeys do not work if a dialog is raised on top of the
				// VNC viewer window, so we have to intercept CTRL-ALT-SHIFT-G
				// at the low level to ensure that the keyboard can still be
				// ungrabbed in this case.  Otherwise, if the viewer is in
				// full-screen mode and the user Alt-Tabs away from the dialog
				// box back to the viewer window, they would be left with no
				// way to exit, ungrab the keyboard, or leave full-screen mode.
				case 'G':
				{
					static BOOL fCtrlAltShiftG = FALSE;
					if (GetAsyncKeyState(VK_MENU) & 0x8000 &&
						GetAsyncKeyState(VK_CONTROL) & 0x8000 &&
						GetAsyncKeyState(VK_SHIFT) & 0x8000 && fKeyDown) {
						fHandled = TRUE;
						fCtrlAltShiftG = TRUE;
					} else if (fCtrlAltShiftG) {
						if (!fKeyDown) {
							PostMessage(g_hwndVNCViewer, WM_SYSCOMMAND,
								ID_TOGGLE_GRAB, 0);
							fHandled = TRUE;
						}
						fCtrlAltShiftG = FALSE;
					}
					break;
				}

			} // switch(pkbdllhook->vkCode)

		} // if (ProcessID == g_VncProcesID)

	} // if (nCode == HT_ACTION)

	// Call the next hook, if we didn't handle this message
    return (fHandled ? TRUE : CallNextHookEx(g_HookID, nCode, wParam, lParam));
}