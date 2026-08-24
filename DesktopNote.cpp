#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define ID_NOTE     1001
#define ID_UNLOCK   1002
#define ID_SETTINGS 1003
#define ID_TIMER    1004
#define ID_PWD_EDIT 2001
#define ID_PWD_OK   2002
#define ID_PWD_CANCEL 2003

static HWND gMain = nullptr;
static HWND gNote = nullptr;
static HWND gStatus = nullptr;
static HFONT gFont = nullptr;
static bool gUnlocked = false;
static std::wstring gNotePath;

static const wchar_t* DEFAULT_PASSWORD = L"1234";

static void ApplyFont(HWND h) {
    if (!h || !gFont) return;
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
}

static std::wstring AppDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    const size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : s.substr(0, pos);
}

static void SaveNote() {
    if (!gNote) return;
    const int n = GetWindowTextLengthW(gNote);
    std::wstring text(static_cast<size_t>(n), L'\0');
    if (n > 0) GetWindowTextW(gNote, &text[0], n + 1);

    std::wofstream f(gNotePath, std::ios::trunc);
    if (f) f << text;
}

static void LoadNote() {
    std::wifstream f(gNotePath);
    std::wstringstream ss;
    ss << f.rdbuf();
    const std::wstring text = ss.str();
    SetWindowTextW(gNote, text.c_str());
}

static void UpdateWindowTitle() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t title[256]{};
    swprintf_s(title, L"Desktop Note  |  %02d/%02d/%04d", st.wDay, st.wMonth, st.wYear);
    SetWindowTextW(gMain, title);
}

static void UpdateStatus() {
    SetWindowTextW(gStatus, gUnlocked ? L"UNLOCKED - Đã mở khóa" : L"LOCKED - Đang khóa");
    SetWindowTextW(GetDlgItem(gMain, ID_UNLOCK),
                   gUnlocked ? L"Khóa lại" : L"Mở khóa");
    EnableWindow(gNote, gUnlocked);
    EnableWindow(GetDlgItem(gMain, ID_SETTINGS), gUnlocked);

    LONG_PTR style = GetWindowLongPtrW(gMain, GWL_STYLE);
    if (gUnlocked)
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    else
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(gMain, GWL_STYLE, style);
    SetWindowPos(gMain, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

struct PasswordPromptState {
    HWND window = nullptr;
    HWND edit = nullptr;
    bool accepted = false;
};

static HFONT gPasswordFont = nullptr;

static LRESULT CALLBACK PasswordWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    PasswordPromptState* s =
        reinterpret_cast<PasswordPromptState*>(GetWindowLongPtrW(h, GWLP_USERDATA));

    if (m == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = reinterpret_cast<PasswordPromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        s->window = h;
    }

    switch (m) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Nhập mật khẩu:",
            WS_CHILD | WS_VISIBLE, 20, 18, 220, 24, h, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        s->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
            20, 48, 300, 30, h, (HMENU)ID_PWD_EDIT,
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"BUTTON", L"Xác nhận",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            145, 92, 85, 30, h, (HMENU)ID_PWD_OK,
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"BUTTON", L"Huy",
            WS_CHILD | WS_VISIBLE,
            235, 92, 85, 30, h, (HMENU)ID_PWD_CANCEL,
            GetModuleHandleW(nullptr), nullptr);

        gPasswordFont = CreateFontW(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        for (HWND child = GetWindow(h, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(gPasswordFont), TRUE);

        SetFocus(s->edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_PWD_OK) {
            wchar_t buf[128]{};
            GetWindowTextW(s->edit, buf, 128);
            if (std::wstring(buf) == DEFAULT_PASSWORD) {
                s->accepted = true;
                DestroyWindow(h);
            } else {
                MessageBoxW(h, L"Mật khẩu không đúng.", L"Desktop Note",
                            MB_OK | MB_ICONWARNING);
                SetWindowTextW(s->edit, L"");
                SetFocus(s->edit);
            }
            return 0;
        }
        if (LOWORD(wp) == ID_PWD_CANCEL) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (gPasswordFont) {
            DeleteObject(gPasswordFont);
            gPasswordFont = nullptr;
        }
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool AskPassword(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PasswordWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DesktopNotePasswordWindow";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    PasswordPromptState state{};

    EnableWindow(owner, FALSE);
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"DesktopNotePasswordWindow",
        L"Desktop Note - Xác thực",
        WS_CAPTION | WS_SYSMENU,
        0, 0, 350, 165,
        owner, nullptr, GetModuleHandleW(nullptr), &state);

    RECT r{}, o{};
    GetWindowRect(dlg, &r);
    GetWindowRect(owner, &o);
    const int x = o.left + ((o.right - o.left) - (r.right - r.left)) / 2;
    const int y = o.top + ((o.bottom - o.top) - (r.bottom - r.top)) / 2;
    SetWindowPos(dlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg{};
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.accepted;
}

static void SetLocked(bool locked) {
    gUnlocked = !locked;
    UpdateStatus();
}

static LRESULT CALLBACK MainWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        gMain = h;
        gNotePath = AppDir() + L"\\DesktopNote.txt";

        // Explicit Unicode-capable Windows UI font.
        gFont = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        CreateWindowW(L"STATIC", L"LỊCH ÂM / DƯƠNG",
            WS_CHILD | WS_VISIBLE, 20, 18, 250, 25, h, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"STATIC", L"Đồng hồ",
            WS_CHILD | WS_VISIBLE, 420, 18, 250, 25, h, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"STATIC", L"Desktop Note",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 400, 45, 270, 45, h, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"STATIC", L"Lịch dương hôm nay",
            WS_CHILD | WS_VISIBLE, 20, 50, 250, 25, h, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        gNote = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | WS_VSCROLL,
            20, 100, 680, 260, h, (HMENU)ID_NOTE,
            GetModuleHandleW(nullptr), nullptr);

        gStatus = CreateWindowW(
            L"STATIC", L"DANG KHOA",
            WS_CHILD | WS_VISIBLE, 20, 380, 260, 25, h, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"BUTTON", L"Mở khóa",
            WS_CHILD | WS_VISIBLE, 470, 375, 105, 32, h,
            (HMENU)ID_UNLOCK, GetModuleHandleW(nullptr), nullptr);

        CreateWindowW(L"BUTTON", L"Cài đặt",
            WS_CHILD | WS_VISIBLE, 585, 375, 105, 32, h,
            (HMENU)ID_SETTINGS, GetModuleHandleW(nullptr), nullptr);

        // Apply Segoe UI to all controls so Vietnamese diacritics render correctly.
        for (HWND child = GetWindow(h, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            ApplyFont(child);

        LoadNote();
        SetLocked(true);
        SetTimer(h, ID_TIMER, 1000, nullptr);
        UpdateWindowTitle();
        return 0;
    }

    case WM_TIMER:
        UpdateWindowTitle();
        return 0;

    case WM_NCHITTEST:
        if (!gUnlocked) return HTCLIENT; // prevents dragging while locked
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_UNLOCK) {
            if (gUnlocked) {
                SaveNote();
                SetLocked(true);
            } else if (AskPassword(h)) {
                SetLocked(false);
            }
            return 0;
        }

        if (LOWORD(wp) == ID_SETTINGS) {
            if (!gUnlocked) return 0;
            MessageBoxW(h,
                L"Cài đặt chỉ có thể thay đổi khi đã xác thực mật khẩu.\n\n"
                L"Bản V1 đã bật cơ chế khóa toàn bộ thao tác thay đổi.",
                L"Desktop Note - Cài đặt",
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        if (LOWORD(wp) == ID_NOTE && HIWORD(wp) == EN_CHANGE && gUnlocked) {
            SaveNote();
            return 0;
        }
        break;

    case WM_SIZE: {
        const int w = LOWORD(lp);
        const int hh = HIWORD(lp);
        MoveWindow(gNote, 20, 100, max(200, w - 40), max(120, hh - 165), TRUE);
        MoveWindow(gStatus, 20, hh - 55, 300, 25, TRUE);
        MoveWindow(GetDlgItem(h, ID_UNLOCK), max(20, w - 235), hh - 60, 105, 32, TRUE);
        MoveWindow(GetDlgItem(h, ID_SETTINGS), max(20, w - 120), hh - 60, 105, 32, TRUE);
        return 0;
    }

    case WM_CLOSE:
        if (!gUnlocked) {
            MessageBoxW(h,
                L"Ứng dụng đang khóa.\nPhải mở khóa bằng mật khẩu trước khi đóng.",
                L"Desktop Note", MB_OK | MB_ICONWARNING);
            return 0;
        }
        SaveNote();
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h, ID_TIMER);
        if (gFont) {
            DeleteObject(gFont);
            gFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSW wc{};
    wc.hInstance = hInst;
    wc.lpfnWndProc = MainWndProc;
    wc.lpszClassName = L"DesktopNoteMainWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND h = CreateWindowExW(
        WS_EX_TOPMOST,
        L"DesktopNoteMainWindow",
        L"Desktop Note",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        100, 100, 740, 470,
        nullptr, nullptr, hInst, nullptr);

    if (!h) return 1;

    ShowWindow(h, nCmdShow);
    UpdateWindow(h);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
