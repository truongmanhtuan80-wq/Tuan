#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

// ============================================================
// Desktop Note - Complete Win32 V2
// 3 areas: Calendar (solar/lunar), Clock, Note
// Locked by default. Every modification requires password unlock.
// Default password: 1234
// ============================================================

#define IDC_NOTE        1001
#define IDC_UNLOCK      1002
#define IDC_SETTINGS    1003
#define IDC_LOCK        1004

#define IDT_CLOCK       1101
#define IDT_AUTOSAVE    1102

#define ID_CAL_PREV     2001
#define ID_CAL_NEXT     2002
#define ID_CAL_TODAY    2003

#define ID_SET_THEME    3001
#define ID_SET_SECONDS  3002
#define ID_SET_LUNAR    3003
#define ID_SET_FONT     3004
#define ID_SET_OPACITY  3005
#define ID_SET_CLOCK    3006
#define ID_SET_PASSWORD 3007
#define ID_SET_SAVE     3008
#define ID_SET_CANCEL   3009

static HWND gMain = nullptr;
static HWND gNote = nullptr;
static HWND gStatus = nullptr;
static HWND gCalendar = nullptr;
static HFONT gUiFont = nullptr;
static HFONT gNoteFont = nullptr;
static bool gUnlocked = false;
static bool gSettingsOpen = false;

static SYSTEMTIME gCalendarMonth{};
static std::wstring gDataDir;
static std::wstring gNoteFile;
static std::wstring gConfigFile;

struct AppConfig {
    bool showSeconds = true;
    bool showLunar = true;
    int noteFontSize = 18;
    int opacity = 96;
    int theme = 0;       // 0 light, 1 dark
    int clockStyle = 0;  // 0 digital, 1 large
    std::wstring password = L"1234";
};

static AppConfig gCfg;

static COLORREF BgColor() { return gCfg.theme ? RGB(32,34,37) : RGB(247,247,245); }
static COLORREF PanelColor() { return gCfg.theme ? RGB(45,47,51) : RGB(255,255,255); }
static COLORREF TextColor() { return gCfg.theme ? RGB(240,240,240) : RGB(35,35,35); }
static COLORREF MutedColor() { return gCfg.theme ? RGB(185,185,185) : RGB(100,100,100); }
static COLORREF AccentColor() { return gCfg.theme ? RGB(120,180,255) : RGB(70,110,180); }

static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    size_t b = s.find_last_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    return s.substr(a, b - a + 1);
}

static std::wstring GetModuleDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : s.substr(0, pos);
}

static void WriteText(const std::wstring& path, const std::wstring& value) {
    std::wofstream f(path, std::ios::trunc);
    if (f) f << value;
}

static std::wstring ReadText(const std::wstring& path) {
    std::wifstream f(path);
    std::wstringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void SaveConfig() {
    // Simple line-based UTF-8-independent local config.
    std::wofstream f(gConfigFile, std::ios::trunc);
    if (!f) return;
    f << L"showSeconds=" << (gCfg.showSeconds ? 1 : 0) << L"\n";
    f << L"showLunar=" << (gCfg.showLunar ? 1 : 0) << L"\n";
    f << L"noteFontSize=" << gCfg.noteFontSize << L"\n";
    f << L"opacity=" << gCfg.opacity << L"\n";
    f << L"theme=" << gCfg.theme << L"\n";
    f << L"clockStyle=" << gCfg.clockStyle << L"\n";
    f << L"password=" << gCfg.password << L"\n";
}

static int IntValue(const std::wstring& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

static void LoadConfig() {
    std::wstring text = ReadText(gConfigFile);
    if (text.empty()) {
        SaveConfig();
        return;
    }
    std::wistringstream in(text);
    std::wstring line;
    while (std::getline(in, line)) {
        size_t p = line.find(L'=');
        if (p == std::wstring::npos) continue;
        std::wstring k = Trim(line.substr(0,p));
        std::wstring v = Trim(line.substr(p+1));
        if (k == L"showSeconds") gCfg.showSeconds = IntValue(v,1) != 0;
        else if (k == L"showLunar") gCfg.showLunar = IntValue(v,1) != 0;
        else if (k == L"noteFontSize") gCfg.noteFontSize = std::clamp(IntValue(v,18),12,30);
        else if (k == L"opacity") gCfg.opacity = std::clamp(IntValue(v,96),55,100);
        else if (k == L"theme") gCfg.theme = std::clamp(IntValue(v,0),0,1);
        else if (k == L"clockStyle") gCfg.clockStyle = std::clamp(IntValue(v,0),0,1);
        else if (k == L"password" && !v.empty()) gCfg.password = v;
    }
}

static void SaveNote() {
    if (!gNote) return;
    int n = GetWindowTextLengthW(gNote);
    std::wstring s(static_cast<size_t>(n), L'\0');
    if (n > 0) GetWindowTextW(gNote, s.data(), n + 1);
    WriteText(gNoteFile, s);
}

static void LoadNote() {
    std::wstring s = ReadText(gNoteFile);
    SetWindowTextW(gNote, s.c_str());
}

static void ApplyFont(HWND h, HFONT f) {
    if (h && f) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
}

static void CreateFonts() {
    if (gUiFont) DeleteObject(gUiFont);
    if (gNoteFont) DeleteObject(gNoteFont);

    gUiFont = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    gNoteFont = CreateFontW(-gCfg.noteFontSize,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
}

static void ApplyFontsToChildren(HWND parent) {
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        ApplyFont(c, c == gNote ? gNoteFont : gUiFont);
    }
}

static void ApplyOpacity() {
    SetLayeredWindowAttributes(gMain, 0, static_cast<BYTE>(255 * gCfg.opacity / 100), LWA_ALPHA);
}

static int JulianDay(int d, int m, int y) {
    int a = (14-m)/12;
    int yy = y+4800-a;
    int mm = m+12*a-3;
    return d+(153*mm+2)/5+365*yy+yy/4-yy/100+yy/400-32045;
}

static int NewMoon(int k) {
    double T = k / 1236.85;
    double T2=T*T, T3=T2*T;
    double dr=M_PI/180.0;
    double jd = 2415020.75933 + 29.53058868*k + 0.0001178*T2 - 0.000000155*T3;
    jd += 0.00033*sin((166.56+132.87*T-0.009173*T2)*dr);
    double M=359.2242+29.10535608*k-0.0000333*T2-0.00000347*T3;
    double Mp=306.0253+385.81691806*k+0.0107306*T2+0.00001236*T3;
    double F=21.2964+390.67050646*k-0.0016528*T2-0.00000239*T3;
    double C=(0.1734-0.000393*T)*sin(M*dr)
        +0.0021*sin(2*M*dr)-0.4068*sin(Mp*dr)
        +0.0161*sin(2*Mp*dr)-0.0004*sin(3*Mp*dr)
        +0.0104*sin(2*F*dr)-0.0051*sin((M+Mp)*dr)
        -0.0074*sin((M-Mp)*dr)+0.0004*sin((2*F+M)*dr)
        -0.0004*sin((2*F-M)*dr)-0.0006*sin((2*F+Mp)*dr)
        +0.0010*sin((2*F-Mp)*dr)+0.0005*sin((2*Mp+M)*dr);
    double dt = T < -11
        ? 0.001+0.000839*T+0.0002261*T2-0.00000845*T3-0.000000081*T*T3
        : -0.000278+0.000265*T+0.000262*T2;
    return static_cast<int>(floor(jd+C-dt+0.5+7.0/24.0));
}

static int SunLongitudeSector(int jdn) {
    double T=(jdn-2451545.5)/36525.0, T2=T*T, dr=M_PI/180.0;
    double M=357.52910+35999.05030*T-0.0001559*T2-0.00000048*T2*T;
    double L0=280.46645+36000.76983*T+0.0003032*T2;
    double DL=(1.914600-0.004817*T-0.000014*T2)*sin(dr*M)
        +(0.019993-0.000101*T)*sin(dr*2*M)+0.000290*sin(dr*3*M);
    double L=(L0+DL)*dr;
    L-=2*M_PI*floor(L/(2*M_PI));
    return static_cast<int>(floor(L/M_PI*6));
}

static int LunarMonth11(int y) {
    int off=JulianDay(31,12,y)-JulianDay(31,1,1900);
    int k=static_cast<int>(floor(off/29.530588853));
    int nm=NewMoon(k);
    if (SunLongitudeSector(nm)>=9) nm=NewMoon(k-1);
    return nm;
}

static int LeapMonthOffset(int a11) {
    int k=static_cast<int>(floor(0.5+(a11-2415021.076998695)/29.530588853));
    int last=0, i=1;
    int arc=SunLongitudeSector(NewMoon(k+i));
    do {
        last=arc; ++i;
        arc=SunLongitudeSector(NewMoon(k+i));
    } while (arc!=last && i<14);
    return i-1;
}

struct LunarDate { int day=1, month=1, year=2000; bool leap=false; };

static LunarDate SolarToLunar(int dd,int mm,int yy) {
    int dayNumber=JulianDay(dd,mm,yy);
    int k=static_cast<int>(floor((dayNumber-2415021.076998695)/29.530588853));
    int monthStart=NewMoon(k+1);
    if (monthStart>dayNumber) monthStart=NewMoon(k);
    int a11=LunarMonth11(yy), b11=a11, lunarYear;
    if (a11>=monthStart) {
        lunarYear=yy; a11=LunarMonth11(yy-1);
    } else {
        lunarYear=yy+1; b11=LunarMonth11(yy+1);
    }
    int lunarDay=dayNumber-monthStart+1;
    int diff=static_cast<int>(floor((monthStart-a11)/29.0));
    int lunarMonth=diff+11;
    bool leap=false;
    if (b11-a11>365) {
        int leapDiff=LeapMonthOffset(a11);
        if (diff>=leapDiff) {
            lunarMonth=diff+10;
            if (diff==leapDiff) leap=true;
        }
    }
    if (lunarMonth>12) lunarMonth-=12;
    if (lunarMonth>=11 && diff<4) --lunarYear;
    return {lunarDay,lunarMonth,lunarYear,leap};
}

static std::wstring LunarText(int d,int m,int y) {
    LunarDate l=SolarToLunar(d,m,y);
    wchar_t b[128]{};
    swprintf_s(b,L"%02d/%02d âm lịch%s",l.day,l.month,l.leap?L" (nhuận)":L"");
    return b;
}

static int DaysInMonth(int y,int m) {
    if (m==2) return ((y%4==0 && y%100!=0) || y%400==0) ? 29 : 28;
    return (m==4||m==6||m==9||m==11) ? 30 : 31;
}

static std::wstring WeekdayVN(int w) {
    static const wchar_t* a[] = {L"Chủ nhật",L"Thứ Hai",L"Thứ Ba",L"Thứ Tư",L"Thứ Năm",L"Thứ Sáu",L"Thứ Bảy"};
    return a[std::clamp(w,0,6)];
}

static void TodayToCalendar() {
    GetLocalTime(&gCalendarMonth);
    gCalendarMonth.wDay=1;
}

static void ChangeCalendarMonth(int delta) {
    int m=static_cast<int>(gCalendarMonth.wMonth)+delta;
    int y=gCalendarMonth.wYear;
    if (m<1) {m=12;--y;}
    if (m>12) {m=1;++y;}
    gCalendarMonth.wMonth=static_cast<WORD>(m);
    gCalendarMonth.wYear=static_cast<WORD>(y);
    InvalidateRect(gCalendar,nullptr,TRUE);
}

static void DrawTextCenter(HDC dc, const std::wstring& s, RECT r, UINT flags) {
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags|DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static LRESULT CALLBACK CalendarProc(HWND h,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_LBUTTONDOWN: {
        int w=LOWORD(lp), hh=HIWORD(lp);
        RECT r{}; GetClientRect(h,&r);
        int top=54, rowH=(r.bottom-top)/6;
        if (hh<44 && w<65) { ChangeCalendarMonth(-1); return 0; }
        if (hh<44 && w>r.right-65) { ChangeCalendarMonth(1); return 0; }
        if (hh<44 && w>=r.right/2-60 && w<=r.right/2+60) { TodayToCalendar(); InvalidateRect(h,nullptr,TRUE); return 0; }
        if (hh>=top) {
            int col=w/(r.right/7);
            int row=(hh-top)/rowH;
            int first=JulianDay(1,gCalendarMonth.wMonth,gCalendarMonth.wYear);
            int firstCol=(first+1)%7; // Sunday=0
            int day=row*7+col-firstCol+1;
            if(day>=1 && day<=DaysInMonth(gCalendarMonth.wYear,gCalendarMonth.wMonth)) {
                // no destructive action; calendar is informational
            }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(h,&ps);
        RECT r{}; GetClientRect(h,&r);
        HBRUSH bg=CreateSolidBrush(PanelColor()); FillRect(dc,&r,bg); DeleteObject(bg);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,TextColor());

        RECT head{0,0,r.right,44};
        HBRUSH hb=CreateSolidBrush(gCfg.theme?RGB(55,58,64):RGB(238,239,240)); FillRect(dc,&head,hb); DeleteObject(hb);

        HFONT old=(HFONT)SelectObject(dc,gUiFont);
        wchar_t title[128]{};
        swprintf_s(title,L"%04d年?"); // overwritten below
        swprintf_s(title,L"%04d/%02d",gCalendarMonth.wYear,gCalendarMonth.wMonth);
        DrawTextCenter(dc,title,RECT{65,0,r.right-65,44},DT_CENTER);
        DrawTextCenter(dc,L"‹",RECT{0,0,55,44},DT_CENTER);
        DrawTextCenter(dc,L"›",RECT{r.right-55,0,r.right,44},DT_CENTER);

        const wchar_t* days[]={L"CN",L"T2",L"T3",L"T4",L"T5",L"T6",L"T7"};
        int colW=r.right/7;
        for(int i=0;i<7;++i) {
            RECT dr{i*colW,45,(i+1)*colW,68};
            SetTextColor(dc,MutedColor());
            DrawTextCenter(dc,days[i],dr,DT_CENTER);
        }

        int top=68;
        int rowH=(r.bottom-top)/6;
        int first=JulianDay(1,gCalendarMonth.wMonth,gCalendarMonth.wYear);
        int firstCol=(first+1)%7;
        SYSTEMTIME now{}; GetLocalTime(&now);

        for(int cell=0;cell<42;++cell) {
            int day=cell-firstCol+1;
            int row=cell/7,col=cell%7;
            RECT cr{col*colW,top+row*rowH,(col+1)*colW,top+(row+1)*rowH};
            if(day<1 || day>DaysInMonth(gCalendarMonth.wYear,gCalendarMonth.wMonth)) continue;

            bool today=(day==now.wDay && gCalendarMonth.wMonth==now.wMonth && gCalendarMonth.wYear==now.wYear);
            if(today) {
                HBRUSH tb=CreateSolidBrush(gCfg.theme?RGB(50,100,155):RGB(220,235,255));
                FillRect(dc,&cr,tb); DeleteObject(tb);
            }
            wchar_t s[64]{};
            LunarDate ld=SolarToLunar(day,gCalendarMonth.wMonth,gCalendarMonth.wYear);
            swprintf_s(s,L"%d",day);
            SetTextColor(dc,TextColor());
            DrawTextCenter(dc,s,RECT{cr.left,cr.top+2,cr.right,cr.top+24},DT_CENTER);
            if(gCfg.showLunar) {
                wchar_t ls[32]{};
                swprintf_s(ls,L"%d/%d",ld.day,ld.month);
                SetTextColor(dc,MutedColor());
                DrawTextCenter(dc,ls,RECT{cr.left,cr.top+24,cr.right,cr.bottom-2},DT_CENTER);
            }
        }
        SelectObject(dc,old);
        EndPaint(h,&ps);
        return 0;
    }
    }
    return DefWindowProcW(h,m,wp,lp);
}

static void PaintClock(HDC dc, RECT r) {
    HBRUSH b=CreateSolidBrush(PanelColor()); FillRect(dc,&r,b); DeleteObject(b);
    SetBkMode(dc,TRANSPARENT);
    SYSTEMTIME st{}; GetLocalTime(&st);

    wchar_t timeBuf[32]{};
    if(gCfg.showSeconds) swprintf_s(timeBuf,L"%02d:%02d:%02d",st.wHour,st.wMinute,st.wSecond);
    else swprintf_s(timeBuf,L"%02d:%02d",st.wHour,st.wMinute);

    HFONT f=CreateFontW(gCfg.clockStyle? -58:-44,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT old=(HFONT)SelectObject(dc,f);
    SetTextColor(dc,AccentColor());
    DrawTextW(dc,timeBuf,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,old); DeleteObject(f);

    RECT sub=r; sub.top+=75;
    wchar_t dateBuf[128]{};
    swprintf_s(dateBuf,L"%s  •  %02d/%02d/%04d",WeekdayVN((st.wDayOfWeek+0)%7).c_str(),
        st.wDay,st.wMonth,st.wYear);
    HFONT sf=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    old=(HFONT)SelectObject(dc,sf);
    SetTextColor(dc,MutedColor());
    DrawTextW(dc,dateBuf,-1,&sub,DT_CENTER|DT_SINGLELINE);
    SelectObject(dc,old); DeleteObject(sf);
}

static void PaintHeader(HWND h) {
    InvalidateRect(h,nullptr,TRUE);
    InvalidateRect(gCalendar,nullptr,TRUE);
}

static void SetLocked(bool locked) {
    gUnlocked=!locked;
    EnableWindow(gNote,gUnlocked);
    EnableWindow(GetDlgItem(gMain,IDC_SETTINGS),gUnlocked);

    // Locked = cannot move/resize/close. Unlock = normal window editing.
    LONG_PTR style=GetWindowLongPtrW(gMain,GWL_STYLE);
    if(gUnlocked) style|=WS_THICKFRAME|WS_MAXIMIZEBOX;
    else style&=~(WS_THICKFRAME|WS_MAXIMIZEBOX);
    SetWindowLongPtrW(gMain,GWL_STYLE,style);
    SetWindowPos(gMain,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);

    SetWindowTextW(GetDlgItem(gMain,IDC_UNLOCK),gUnlocked?L"Khóa lại":L"Mở khóa");
    SetWindowTextW(gStatus,gUnlocked?L"ĐÃ MỞ KHÓA":L"ĐANG KHÓA");
    InvalidateRect(gMain,nullptr,TRUE);
}

struct PasswordState { HWND wnd=nullptr; HWND edit=nullptr; bool ok=false; };

static LRESULT CALLBACK PasswordProc(HWND h,UINT m,WPARAM wp,LPARAM lp) {
    PasswordState* s=(PasswordState*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE) {
        auto cs=(CREATESTRUCTW*)lp;
        s=(PasswordState*)cs->lpCreateParams;
        SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)s);
    }
    switch(m) {
    case WM_CREATE:
        CreateWindowW(L"STATIC",L"Nhập mật khẩu:",WS_CHILD|WS_VISIBLE,20,18,250,25,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        s->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_PASSWORD|ES_AUTOHSCROLL,
            20,50,300,30,h,(HMENU)5001,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Xác nhận",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,135,95,90,32,h,(HMENU)5002,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Hủy",WS_CHILD|WS_VISIBLE,235,95,85,32,h,(HMENU)5003,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(s->edit,gUiFont);
        for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) ApplyFont(c,gUiFont);
        SetFocus(s->edit); return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==5002) {
            wchar_t b[256]{}; GetWindowTextW(s->edit,b,256);
            if(std::wstring(b)==gCfg.password) { s->ok=true; DestroyWindow(h); }
            else { MessageBoxW(h,L"Mật khẩu không đúng.",L"Desktop Note",MB_OK|MB_ICONWARNING); SetWindowTextW(s->edit,L""); SetFocus(s->edit); }
            return 0;
        }
        if(LOWORD(wp)==5003) { DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h,m,wp,lp);
}

static bool AskPassword(HWND owner) {
    static bool reg=false;
    if(!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc=PasswordProc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"DesktopNotePassword"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); RegisterClassW(&wc); reg=true;
    }
    PasswordState s{};
    EnableWindow(owner,FALSE);
    HWND d=CreateWindowExW(WS_EX_DLGMODALFRAME,L"DesktopNotePassword",L"Desktop Note - Xác thực",
        WS_CAPTION|WS_SYSMENU,0,0,350,165,owner,nullptr,GetModuleHandleW(nullptr),&s);
    RECT dr{},orx{}; GetWindowRect(d,&dr); GetWindowRect(owner,&orx);
    SetWindowPos(d,HWND_TOP,orx.left+(orx.right-orx.left-dr.right+dr.left)/2,
        orx.top+(orx.bottom-orx.top-dr.bottom+dr.top)/2,0,0,SWP_NOSIZE);
    ShowWindow(d,SW_SHOW); UpdateWindow(d);
    MSG msg{};
    while(IsWindow(d)&&GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    EnableWindow(owner,TRUE); SetForegroundWindow(owner);
    return s.ok;
}

struct SettingsState {
    HWND wnd=nullptr;
    HWND theme=nullptr, seconds=nullptr, lunar=nullptr, font=nullptr, opacity=nullptr, clock=nullptr;
    HWND oldpass=nullptr, newpass=nullptr;
    AppConfig original;
};

static SettingsState* gSet=nullptr;

static void ApplyThemeToControl(HWND c) {
    if(!c) return;
    ApplyFont(c,gUiFont);
}

static LRESULT CALLBACK SettingsProc(HWND h,UINT m,WPARAM wp,LPARAM lp) {
    SettingsState* s=(SettingsState*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE) {
        auto cs=(CREATESTRUCTW*)lp; s=(SettingsState*)cs->lpCreateParams;
        SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)s);
    }
    switch(m) {
    case WM_CREATE: {
        s->theme=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,175,18,180,30,h,(HMENU)ID_SET_THEME,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(s->theme,CB_ADDSTRING,0,(LPARAM)L"Sáng");
        SendMessageW(s->theme,CB_ADDSTRING,0,(LPARAM)L"Tối");
        SendMessageW(s->theme,CB_SETCURSEL,gCfg.theme,0);

        s->seconds=CreateWindowW(L"BUTTON",L"Hiển thị giây",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,20,60,180,28,h,(HMENU)ID_SET_SECONDS,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(s->seconds,BM_SETCHECK,gCfg.showSeconds?BST_CHECKED:BST_UNCHECKED,0);

        s->lunar=CreateWindowW(L"BUTTON",L"Hiển thị lịch âm",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,20,92,180,28,h,(HMENU)ID_SET_LUNAR,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(s->lunar,BM_SETCHECK,gCfg.showLunar?BST_CHECKED:BST_UNCHECKED,0);

        CreateWindowW(L"STATIC",L"Cỡ chữ Note:",WS_CHILD|WS_VISIBLE,20,128,140,25,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        s->font=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_NUMBER,175,124,80,28,h,(HMENU)ID_SET_FONT,GetModuleHandleW(nullptr),nullptr);
        wchar_t b[32]{}; swprintf_s(b,L"%d",gCfg.noteFontSize); SetWindowTextW(s->font,b);

        CreateWindowW(L"STATIC",L"Độ trong suốt:",WS_CHILD|WS_VISIBLE,20,164,140,25,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        s->opacity=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_NUMBER,175,160,80,28,h,(HMENU)ID_SET_OPACITY,GetModuleHandleW(nullptr),nullptr);
        swprintf_s(b,L"%d",gCfg.opacity); SetWindowTextW(s->opacity,b);

        CreateWindowW(L"STATIC",L"Kiểu đồng hồ:",WS_CHILD|WS_VISIBLE,20,200,140,25,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        s->clock=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,175,196,180,30,h,(HMENU)ID_SET_CLOCK,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(s->clock,CB_ADDSTRING,0,(LPARAM)L"Tiêu chuẩn");
        SendMessageW(s->clock,CB_ADDSTRING,0,(LPARAM)L"Lớn");
        SendMessageW(s->clock,CB_SETCURSEL,gCfg.clockStyle,0);

        CreateWindowW(L"STATIC",L"Đổi mật khẩu (không bắt buộc)",WS_CHILD|WS_VISIBLE,20,235,300,25,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"STATIC",L"Mật khẩu mới:",WS_CHILD|WS_VISIBLE,20,267,140,25,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        s->newpass=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_PASSWORD,175,263,180,28,h,(HMENU)ID_SET_PASSWORD,GetModuleHandleW(nullptr),nullptr);

        CreateWindowW(L"BUTTON",L"Lưu",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,175,310,85,32,h,(HMENU)ID_SET_SAVE,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Hủy",WS_CHILD|WS_VISIBLE,270,310,85,32,h,(HMENU)ID_SET_CANCEL,GetModuleHandleW(nullptr),nullptr);

        for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) ApplyThemeToControl(c);
        return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==ID_SET_SAVE) {
            int theme=(int)SendMessageW(s->theme,CB_GETCURSEL,0,0);
            int clock=(int)SendMessageW(s->clock,CB_GETCURSEL,0,0);
            wchar_t b[64]{};
            GetWindowTextW(s->font,b,64); int fs=std::clamp(IntValue(b,18),12,30);
            GetWindowTextW(s->opacity,b,64); int op=std::clamp(IntValue(b,96),55,100);
            wchar_t np[256]{}; GetWindowTextW(s->newpass,np,256);

            gCfg.theme=theme<0?0:theme;
            gCfg.clockStyle=clock<0?0:clock;
            gCfg.showSeconds=SendMessageW(s->seconds,BM_GETCHECK,0,0)==BST_CHECKED;
            gCfg.showLunar=SendMessageW(s->lunar,BM_GETCHECK,0,0)==BST_CHECKED;
            gCfg.noteFontSize=fs;
            gCfg.opacity=op;
            if(np[0]) gCfg.password=np;

            CreateFonts();
            ApplyFont(gNote,gNoteFont);
            ApplyFontsToChildren(gMain);
            ApplyOpacity();
            SaveConfig();
            PaintHeader(gMain);
            DestroyWindow(h);
            gSettingsOpen=false;
            gSet=nullptr;
            return 0;
        }
        if(LOWORD(wp)==ID_SET_CANCEL) { DestroyWindow(h); gSettingsOpen=false; gSet=nullptr; return 0; }
        break;
    case WM_CLOSE: DestroyWindow(h); gSettingsOpen=false; gSet=nullptr; return 0;
    case WM_NCDESTROY: gSet=nullptr; break;
    }
    return DefWindowProcW(h,m,wp,lp);
}

static void OpenSettings(HWND owner) {
    if(!gUnlocked) {
        if(!AskPassword(owner)) return;
        SetLocked(false);
    }
    if(gSettingsOpen && gSet && IsWindow(gSet->wnd)) { SetForegroundWindow(gSet->wnd); return; }

    static bool reg=false;
    if(!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc=SettingsProc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"DesktopNoteSettings"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); RegisterClassW(&wc); reg=true;
    }
    SettingsState* s=new SettingsState();
    s->original=gCfg;
    gSet=s; gSettingsOpen=true;
    HWND d=CreateWindowExW(WS_EX_DLGMODALFRAME,L"DesktopNoteSettings",L"Desktop Note - Cài đặt",
        WS_CAPTION|WS_SYSMENU,0,0,390,390,owner,nullptr,GetModuleHandleW(nullptr),s);
    s->wnd=d;
    RECT dr{},orx{}; GetWindowRect(d,&dr); GetWindowRect(owner,&orx);
    SetWindowPos(d,HWND_TOP,orx.left+(orx.right-orx.left-dr.right+dr.left)/2,
        orx.top+(orx.bottom-orx.top-dr.bottom+dr.top)/2,0,0,SWP_NOSIZE);
    ShowWindow(d,SW_SHOW); UpdateWindow(d);
}

static LRESULT CALLBACK MainProc(HWND h,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_CREATE: {
        gMain=h;
        gDataDir=GetModuleDir();
        gNoteFile=gDataDir+L"\\DesktopNote_Note.txt";
        gConfigFile=gDataDir+L"\\DesktopNote_Config.txt";
        LoadConfig();
        CreateFonts();
        TodayToCalendar();

        gCalendar=CreateWindowExW(0,L"DesktopNoteCalendar",L"",WS_CHILD|WS_VISIBLE,
            18,105,430,310,h,nullptr,GetModuleHandleW(nullptr),nullptr);

        gNote=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,
            465,105,440,310,h,(HMENU)IDC_NOTE,GetModuleHandleW(nullptr),nullptr);

        gStatus=CreateWindowW(L"STATIC",L"ĐANG KHÓA",WS_CHILD|WS_VISIBLE,
            18,425,300,28,h,nullptr,GetModuleHandleW(nullptr),nullptr);

        CreateWindowW(L"BUTTON",L"Mở khóa",WS_CHILD|WS_VISIBLE,650,420,105,34,h,(HMENU)IDC_UNLOCK,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Cài đặt",WS_CHILD|WS_VISIBLE,765,420,105,34,h,(HMENU)IDC_SETTINGS,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Khóa",WS_CHILD|WS_VISIBLE,535,420,105,34,h,(HMENU)IDC_LOCK,GetModuleHandleW(nullptr),nullptr);

        LoadNote();
        ApplyFontsToChildren(h);
        SetLocked(true);
        ApplyOpacity();
        SetTimer(h,IDT_CLOCK,1000,nullptr);
        SetTimer(h,IDT_AUTOSAVE,3000,nullptr);
        return 0;
    }

    case WM_NCHITTEST:
        if(!gUnlocked) return HTCLIENT; // prevents moving by mouse while locked
        break;

    case WM_SYSCOMMAND:
        if(!gUnlocked && ((wp & 0xFFF0)==SC_CLOSE || (wp & 0xFFF0)==SC_MOVE || (wp & 0xFFF0)==SC_SIZE))
            return 0;
        break;

    case WM_COMMAND:
        if(LOWORD(wp)==IDC_UNLOCK) {
            if(gUnlocked) { SaveNote(); SetLocked(true); }
            else if(AskPassword(h)) SetLocked(false);
            return 0;
        }
        if(LOWORD(wp)==IDC_LOCK) {
            if(gUnlocked) { SaveNote(); SetLocked(true); }
            return 0;
        }
        if(LOWORD(wp)==IDC_SETTINGS) {
            OpenSettings(h);
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE && HIWORD(wp)==EN_CHANGE && gUnlocked) {
            // autosaved every few seconds; this avoids disk writes on every keystroke
            return 0;
        }
        break;

    case WM_TIMER:
        if(wp==IDT_CLOCK) {
            InvalidateRect(h,nullptr,TRUE);
            InvalidateRect(gCalendar,nullptr,TRUE);
        } else if(wp==IDT_AUTOSAVE && gUnlocked) SaveNote();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(h,&ps);
        RECT r{}; GetClientRect(h,&r);
        HBRUSH bg=CreateSolidBrush(BgColor()); FillRect(dc,&r,bg); DeleteObject(bg);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,TextColor());

        HFONT old=(HFONT)SelectObject(dc,gUiFont);
        RECT titleR{18,15,450,50};
        DrawTextW(dc,L"DESKTOP NOTE",-1,&titleR,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t date[128]{};
        swprintf_s(date,L"%s  •  %02d/%02d/%04d",WeekdayVN(st.wDayOfWeek).c_str(),st.wDay,st.wMonth,st.wYear);
        SetTextColor(dc,MutedColor());
        RECT dateR{465,15,900,50};
        DrawTextW(dc,date,-1,&dateR,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

        // Clock panel
        RECT cr{465,55,905,100};
        PaintClock(dc,cr);

        SelectObject(dc,old);
        EndPaint(h,&ps);
        return 0;
    }

    case WM_SIZE: {
        int w=LOWORD(lp), hh=HIWORD(lp);
        int leftW=std::max(300,(w-55)/2);
        int rightX=leftW+35;
        int rightW=std::max(300,w-rightX-20);
        MoveWindow(gCalendar,18,105,leftW-25,std::max(220,hh-170),TRUE);
        MoveWindow(gNote,rightX,105,rightW,std::max(220,hh-170),TRUE);
        MoveWindow(gStatus,18,hh-55,300,28,TRUE);
        MoveWindow(GetDlgItem(h,IDC_LOCK),rightX,hh-60,105,34,TRUE);
        MoveWindow(GetDlgItem(h,IDC_UNLOCK),rightX+115,hh-60,105,34,TRUE);
        MoveWindow(GetDlgItem(h,IDC_SETTINGS),rightX+230,hh-60,105,34,TRUE);
        return 0;
    }

    case WM_CLOSE:
        if(!gUnlocked) {
            MessageBoxW(h,L"Ứng dụng đang khóa.\nMở khóa bằng mật khẩu trước khi đóng.",L"Desktop Note",MB_OK|MB_ICONWARNING);
            return 0;
        }
        SaveNote();
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h,IDT_CLOCK); KillTimer(h,IDT_AUTOSAVE);
        if(gNoteFont) DeleteObject(gNoteFont);
        if(gUiFont) DeleteObject(gUiFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h,m,wp,lp);
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int nCmdShow) {
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&ic);

    WNDCLASSW cal{};
    cal.hInstance=hInst; cal.lpfnWndProc=CalendarProc; cal.lpszClassName=L"DesktopNoteCalendar";
    cal.hCursor=LoadCursorW(nullptr,IDC_ARROW); cal.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&cal);

    WNDCLASSW main{};
    main.hInstance=hInst; main.lpfnWndProc=MainProc; main.lpszClassName=L"DesktopNoteMain";
    main.hCursor=LoadCursorW(nullptr,IDC_ARROW); main.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&main);

    HWND h=CreateWindowExW(
        WS_EX_TOPMOST|WS_EX_LAYERED,
        L"DesktopNoteMain",L"Desktop Note",
        WS_OVERLAPPEDWINDOW,
        100,100,940,520,nullptr,nullptr,hInst,nullptr);

    if(!h) return 1;
    ShowWindow(h,nCmdShow); UpdateWindow(h);

    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
