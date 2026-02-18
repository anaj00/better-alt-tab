#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// ============================================================
// CONFIG
// ============================================================

#define WIN_W       700
#define MAX_VISIBLE 8
#define PADDING_V   14
#define ROUND_RAD   18

#define WINDOW_ALPHA 245

#define TITLE_FONT_SIZE 22
#define SUB_FONT_SIZE   14

// Row height derived from font sizes (this prevents clipping)
#define ITEM_H (TITLE_FONT_SIZE + SUB_FONT_SIZE + 44)

#define WM_SHOW_SWITCHER (WM_USER + 1)
#define WM_DO_SWITCH     (WM_USER + 2)
#define WM_MOVE_SEL      (WM_USER + 3)
#define WM_HIDE_SWITCHER (WM_USER + 4)
#define WM_TRAY          (WM_USER + 5)

#define ID_TRAY_EXIT     1001

// ============================================================
// COLORS
// ============================================================

#define BG_COLOR        RGB(18, 18, 20)
#define SEL_BG_COLOR    RGB(45, 45, 55)
#define TITLE_COLOR     RGB(245, 245, 250)
#define SUB_COLOR       RGB(160, 160, 175)
#define DIVIDER_COLOR   RGB(38, 38, 45)

// ============================================================
// STATE
// ============================================================

struct WindowInfo {
    HWND hwnd;
    std::wstring title;
    HICON icon;
};

std::vector<WindowInfo> g_windows;

HWND g_popupWindow = NULL;
HWND g_listBox = NULL;

std::atomic<bool> g_isReady{ false };

HHOOK g_keyboardHook = NULL;
DWORD g_hookThreadId = 0;

HFONT g_fontTitle = NULL;
HFONT g_fontSub = NULL;

HBRUSH g_bgBrush = NULL;

NOTIFYICONDATAW g_nid = {};

// ============================================================
// ACRYLIC BLUR (Windows 10/11 undocumented API)
// ============================================================

typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    int Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(
    HWND, WINDOWCOMPOSITIONATTRIBDATA*
);

void EnableAcrylic(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return;

    auto SetWCA = (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!SetWCA) return;

    ACCENT_POLICY policy = {};
    policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.AccentFlags = 2;

    // GradientColor = AABBGGRR
    // AA = opacity
    policy.GradientColor = (0xCC << 24) | (0x202020);

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = 19; // WCA_ACCENT_POLICY
    data.pvData = &policy;
    data.cbData = sizeof(policy);

    SetWCA(hwnd, &data);
}

// ============================================================
// HELPERS
// ============================================================

bool IsTaskbarWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) return false;

    HWND owner = GetWindow(hwnd, GW_OWNER);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    if (exStyle & WS_EX_TOOLWINDOW) return false;
    if (owner && !(exStyle & WS_EX_APPWINDOW)) return false;

    wchar_t title[2];
    GetWindowTextW(hwnd, title, 2);
    if (wcslen(title) == 0) return false;

    return true;
}

HICON GetWindowIcon(HWND hwnd)
{
    HICON icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    if (icon) return icon;

    icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (icon) return icon;

    icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (icon) return icon;

    icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (icon) return icon;

    icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (icon) return icon;

    return LoadIcon(NULL, IDI_APPLICATION);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
{
    if (!IsTaskbarWindow(hwnd))
        return TRUE;

    wchar_t title[512];
    GetWindowTextW(hwnd, title, 512);

    if (wcslen(title) == 0) return TRUE;
    if (wcscmp(title, L"Program Manager") == 0) return TRUE;

    HICON icon = GetWindowIcon(hwnd);

    g_windows.push_back({ hwnd, title, icon });
    return TRUE;
}

void PopulateList()
{
    g_windows.clear();
    EnumWindows(EnumWindowsProc, 0);

    SendMessageW(g_listBox, LB_RESETCONTENT, 0, 0);

    for (auto& w : g_windows)
        SendMessageW(g_listBox, LB_ADDSTRING, 0, (LPARAM)w.title.c_str());

    SendMessageW(g_listBox, LB_SETCURSEL, 0, 0);
}

void ReleaseAltKey()
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void ForceForeground(HWND hwnd)
{
    HWND fg = GetForegroundWindow();
    DWORD fgThread = GetWindowThreadProcessId(fg, NULL);
    DWORD myThread = GetCurrentThreadId();

    AllowSetForegroundWindow(GetCurrentProcessId());
    AttachThreadInput(fgThread, myThread, TRUE);

    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    AttachThreadInput(fgThread, myThread, FALSE);
}

void ResizeToFitList()
{
    int count = (int)g_windows.size();
    int visible = std::min(count, MAX_VISIBLE);

    int height = PADDING_V + visible * ITEM_H + PADDING_V;

    POINT cursor;
    GetCursorPos(&cursor);

    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(mon, &mi);

    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - WIN_W) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2;

    SetWindowPos(g_popupWindow, HWND_TOPMOST, x, y, WIN_W, height, SWP_SHOWWINDOW);
    SetWindowPos(g_listBox, NULL, 0, PADDING_V, WIN_W, visible * ITEM_H, SWP_NOZORDER);

    HRGN rgn = CreateRoundRectRgn(0, 0, WIN_W + 1, height + 1, ROUND_RAD, ROUND_RAD);
    SetWindowRgn(g_popupWindow, rgn, TRUE);
}

void MoveSelection(int delta)
{
    int count = (int)SendMessageW(g_listBox, LB_GETCOUNT, 0, 0);
    if (count <= 0) return;

    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) sel = 0;

    sel = (sel + delta + count) % count;
    SendMessageW(g_listBox, LB_SETCURSEL, sel, 0);
    InvalidateRect(g_listBox, NULL, FALSE);
}

void HideSwitcher()
{
    g_isReady.store(false, std::memory_order_release);
    ShowWindow(g_popupWindow, SW_HIDE);
    ReleaseAltKey();
}

void SwitchToSelected()
{
    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);

    if (sel == LB_ERR || sel < 0 || sel >= (int)g_windows.size())
    {
        HideSwitcher();
        return;
    }

    HWND target = g_windows[sel].hwnd;

    g_isReady.store(false, std::memory_order_release);
    ShowWindow(g_popupWindow, SW_HIDE);

    if (!IsWindow(target))
        return;

    if (IsIconic(target))
        ShowWindow(target, SW_RESTORE);
    else
        ShowWindow(target, SW_SHOW);

    HWND fg = GetForegroundWindow();
    DWORD fgThread = GetWindowThreadProcessId(fg, NULL);
    DWORD targetThread = GetWindowThreadProcessId(target, NULL);

    AttachThreadInput(fgThread, targetThread, TRUE);

    SetForegroundWindow(target);
    BringWindowToTop(target);
    SetActiveWindow(target);
    SetFocus(target);

    AttachThreadInput(fgThread, targetThread, FALSE);

    ReleaseAltKey();
}

void ShowSwitcher()
{
    PopulateList();
    ResizeToFitList();

    ForceForeground(g_popupWindow);
    SetFocus(g_listBox);

    g_isReady.store(true, std::memory_order_release);
}

// ============================================================
// TRAY ICON
// ============================================================

void AddTrayIcon(HWND hwnd)
{
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    wcscpy_s(g_nid.szTip, L"Raycast AltTab Switcher (Right-click to Exit)");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ============================================================
// OWNER DRAW LISTBOX
// ============================================================

void DrawListItem(DRAWITEMSTRUCT* dis)
{
    if (dis->itemID == (UINT)-1)
        return;

    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    HBRUSH bg = CreateSolidBrush(selected ? SEL_BG_COLOR : BG_COLOR);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    wchar_t title[512];
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)title);

    SetBkMode(dc, TRANSPARENT);

    // Icon
    int iconSize = 34;
    int iconX = rc.left + 18;
    int iconY = rc.top + (ITEM_H - iconSize) / 2;

    if (dis->itemID < g_windows.size())
    {
        DrawIconEx(
            dc,
            iconX, iconY,
            g_windows[dis->itemID].icon,
            iconSize, iconSize,
            0, NULL,
            DI_NORMAL
        );
    }

    int textLeft = rc.left + 18 + iconSize + 14;

    // Title
    SelectObject(dc, g_fontTitle);
    SetTextColor(dc, TITLE_COLOR);

    RECT titleRc = rc;
    titleRc.left = textLeft;
    titleRc.top += 12;
    titleRc.right -= 16;

    DrawTextW(dc, title, -1, &titleRc,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_LEFT);

    // Subtitle
    SelectObject(dc, g_fontSub);
    SetTextColor(dc, SUB_COLOR);

    std::wstring subtitle = L"HWND: " + std::to_wstring((UINT_PTR)g_windows[dis->itemID].hwnd);

    RECT subRc = rc;
    subRc.left = textLeft;
    subRc.top += 12 + TITLE_FONT_SIZE + 12;
    subRc.right -= 16;

    DrawTextW(dc, subtitle.c_str(), -1, &subRc,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_LEFT);

    // Divider line
    HPEN pen = CreatePen(PS_SOLID, 1, DIVIDER_COLOR);
    HPEN oldPen = (HPEN)SelectObject(dc, pen);

    MoveToEx(dc, rc.left + 14, rc.bottom - 1, NULL);
    LineTo(dc, rc.right - 14, rc.bottom - 1);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR)
{
    if (msg == WM_KEYDOWN)
    {
        if (wp == VK_RETURN) { SwitchToSelected(); return 0; }
        if (wp == VK_ESCAPE) { HideSwitcher(); return 0; }
        if (wp == VK_DOWN)   { MoveSelection(1); return 0; }
        if (wp == VK_UP)     { MoveSelection(-1); return 0; }
    }

    if (msg == WM_LBUTTONDBLCLK)
    {
        SwitchToSelected();
        return 0;
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================
// HOOK
// ============================================================

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode != HC_ACTION)
        return CallNextHookEx(g_keyboardHook, nCode, wp, lp);

    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;

    if (kb->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_keyboardHook, nCode, wp, lp);

    bool isAltDown = (kb->flags & LLKHF_ALTDOWN) != 0;
    bool isKeyDown = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    bool isKeyUp = (wp == WM_KEYUP || wp == WM_SYSKEYUP);

    if (kb->vkCode == VK_TAB && isAltDown && isKeyDown)
    {
        bool ready = g_isReady.load(std::memory_order_acquire);

        if (!ready)
        {
            PostMessageW(g_popupWindow, WM_SHOW_SWITCHER, 0, 0);
            return 1;
        }

        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        PostMessageW(g_popupWindow, WM_MOVE_SEL, shift ? -1 : 1, 0);
        return 1;
    }

    if (g_isReady.load(std::memory_order_acquire) &&
        isKeyUp &&
        (kb->vkCode == VK_MENU || kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU))
    {
        AllowSetForegroundWindow(GetCurrentProcessId());
        PostMessageW(g_popupWindow, WM_DO_SWITCH, 0, 0);
        return 1;
    }

    if (g_isReady.load(std::memory_order_acquire) &&
        isKeyDown &&
        kb->vkCode == VK_ESCAPE)
    {
        PostMessageW(g_popupWindow, WM_HIDE_SWITCHER, 0, 0);
        return 1;
    }

    return CallNextHookEx(g_keyboardHook, nCode, wp, lp);
}

DWORD WINAPI HookThread(LPVOID)
{
    g_hookThreadId = GetCurrentThreadId();

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardProc,
        GetModuleHandleW(NULL),
        0
    );

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_keyboardHook);
    return 0;
}

// ============================================================
// WINDOW PROC
// ============================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_bgBrush = CreateSolidBrush(BG_COLOR);

        g_listBox = CreateWindowW(
            L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE |
            LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT |
            LBS_NOTIFY,
            0, PADDING_V, WIN_W, MAX_VISIBLE * ITEM_H,
            hwnd, (HMENU)1, GetModuleHandleW(NULL), NULL
        );

        // Fonts
        g_fontTitle = CreateFontW(
            TITLE_FONT_SIZE, 0, 0, 0,
            FW_BOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Consolas"
        );

        g_fontSub = CreateFontW(
            SUB_FONT_SIZE, 0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Consolas"
        );

        // IMPORTANT: tell listbox the actual row height (THIS FIXES CLIPPING)
        SendMessageW(g_listBox, LB_SETITEMHEIGHT, 0, ITEM_H);

        SetWindowSubclass(g_listBox, ListSubclassProc, 1, 0);

        AddTrayIcon(hwnd);

        EnableAcrylic(hwnd);

        return 0;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetBkColor(dc, BG_COLOR);
        SetTextColor(dc, TITLE_COLOR);
        return (LRESULT)g_bgBrush;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID == 1)
        {
            DrawListItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(dc, &rc, g_bgBrush);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SHOW_SWITCHER:
        ShowSwitcher();
        return 0;

    case WM_MOVE_SEL:
        MoveSelection((int)wp);
        return 0;

    case WM_DO_SWITCH:
        SwitchToSelected();
        return 0;

    case WM_HIDE_SWITCHER:
        HideSwitcher();
        return 0;

    case WM_TRAY:
    {
        if (lp == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

            DestroyMenu(menu);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_TRAY_EXIT)
        {
            RemoveTrayIcon();
            PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_DESTROY:
    {
        RemoveTrayIcon();

        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontSub) DeleteObject(g_fontSub);

        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// MAIN
// ============================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AltTabPopupClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    g_popupWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName,
        L"Raycast AltTab Switcher",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, 400,
        NULL, NULL, hInstance, NULL
    );

    if (!g_popupWindow)
        return 0;

    SetLayeredWindowAttributes(g_popupWindow, 0, WINDOW_ALPHA, LWA_ALPHA);

    ShowWindow(g_popupWindow, SW_HIDE);

    CreateThread(NULL, 0, HookThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
