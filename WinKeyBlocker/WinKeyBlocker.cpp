#define UNICODE
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

static const wchar_t* kWndClass = L"WinKeyBlockerHiddenWindow";
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunName = L"WinKeyBlocker";
static const UINT WMAPP_TRAY = WM_APP + 1;

static HHOOK g_hHook = nullptr;
static HWND  g_hWnd = nullptr;
static NOTIFYICONDATA nid = { 0 };

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

UINT8 g_winBlockFlag = 1;

// 현재 실행 파일 전체 경로 반환
static bool GetSelfPath(wchar_t* buf, DWORD cch) {
    DWORD n = GetModuleFileNameW(nullptr, buf, cch);
    return (n > 0 && n < cch);
}

static bool IsAutoStartRegistered() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t val[32768] = { 0 };
    DWORD type = 0, cb = sizeof(val);
    LONG rc = RegGetValueW(hKey, nullptr, kRunName, RRF_RT_REG_SZ, &type, val, &cb);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return false;

    wchar_t self[32768] = { 0 };
    if (!GetSelfPath(self, ARRAYSIZE(self))) return false;

    wchar_t* p = val;
    size_t len = wcslen(val);
    if (len >= 2 && val[0] == L'\"' && val[len - 1] == L'\"') {
        val[len - 1] = L'\0';
        p = val + 1;
    }

    int cmp = CompareStringOrdinal(self, -1, p, -1, TRUE);
    return (cmp == CSTR_EQUAL);
}


// 부팅 자동 실행 등록(현재 사용자)
static void EnsureAutoStart() {
    if (IsAutoStartRegistered()) return;

    wchar_t self[32768] = { 0 };
    if (!GetSelfPath(self, ARRAYSIZE(self))) return;

    wchar_t quoted[32768] = { 0 };
    wsprintfW(quoted, L"\"%s\"", self);

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, kRunName, 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted),
            (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (g_winBlockFlag && nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (p) {
            if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN ||
                    wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// 트레이 메뉴
void ShowTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    if (g_winBlockFlag) {
        AppendMenu(hMenu, MF_STRING, 1001, L"윈도우키 활성화");
    }
    else {
        AppendMenu(hMenu, MF_STRING, 1001, L"윈도우키 비활성화");
    }
    AppendMenu(hMenu, MF_STRING, 1002, L"종료");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}

bool AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    nid.uCallbackMessage = WMAPP_TRAY;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpyn(nid.szTip, L"Windows 키 차단 동작 중", ARRAYSIZE(nid.szTip));
    return Shell_NotifyIcon(NIM_ADD, &nid) == TRUE;
}

void RemoveTrayIcon() {
    if (nid.cbSize) Shell_NotifyIcon(NIM_DELETE, &nid);
    if (nid.hIcon) { DestroyIcon(nid.hIcon); nid.hIcon = nullptr; }
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 자동 시작 등록(최초 1회만 기록, 실패해도 무시)
    EnsureAutoStart();

    // 숨김 메시지 윈도우
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kWndClass;
    RegisterClassEx(&wc);

    g_hWnd = CreateWindowEx(0, kWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!g_hWnd) return 1;

    if (!AddTrayIcon(g_hWnd)) {
        DestroyWindow(g_hWnd);
        return 1;
    }

    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hHook) {
        RemoveTrayIcon();
        DestroyWindow(g_hWnd);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hHook) { UnhookWindowsHookEx(g_hHook); g_hHook = nullptr; }
    RemoveTrayIcon();
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WMAPP_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu(hWnd);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1002) { // 종료
            PostQuitMessage(0);
        }
        else if (LOWORD(wParam) == 1001) {
            g_winBlockFlag = !g_winBlockFlag;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}
