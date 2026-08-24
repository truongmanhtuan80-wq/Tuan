#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _USE_MATH_DEFINES
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cmath>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

// ============================================================
// Desktop Note - Windows x64 V6
// 2 areas: Calendar (solar/lunar) and expandable Note
// Locked by default. Every modification requires password unlock.
// Default password: 1234
// ============================================================

#define IDC_NOTE        1001
#define IDC_UNLOCK      1002
#define IDC_SETTINGS    1003
#define IDC_LOCK        1004

#define IDT_AUTOSAVE    1102

#define ID_CAL_PREV     2001
#define ID_CAL_NEXT     2002
#define ID_CAL_TODAY    2003

#define ID_SET_THEME    3001
#define ID_SET_LUNAR    3003
#define ID_SET_FONT     3004
#define ID_SET_OPACITY  3005
#define ID_SET_PASSWORD 3007
#define ID_SET_SAVE     3008
#define ID_SET_CANCEL   3009
#define ID_SET_TOP      3010
#define ID_SET_AUTOSAVE 3011
#define ID_SET_NOTE_BG 3012
#define ID_SET_NOTE_TEXT 3013
#define ID_SET_ACCENT 3014
#define IDC_NOTE_TOOLBAR 4001
#define IDC_NOTE_FONT 4002
#define IDC_NOTE_SIZE 4003
#define IDC_NOTE_BOLD 4004
#define IDC_NOTE_ITALIC 4005
#define IDC_NOTE_UNDER 4006
#define IDC_NOTE_TEXTCOLOR 4007
#define IDC_NOTE_BGCOLOR 4008
#define IDT_TOOLBAR 1201

static HWND gMain = nullptr;
static HWND gNote = nullptr;
static HWND gStatus = nullptr;
static HWND gCalendar = nullptr;
static HFONT gUiFont = nullptr;
static HFONT gNoteFont = nullptr;
static HBRUSH gNoteBgBrush = nullptr;
static HWND gNoteToolbar = nullptr;
static HWND gNoteFontCombo = nullptr;
static HWND gNoteSizeCombo = nullptr;
static HWND gNoteBold = nullptr;
static HWND gNoteItalic = nullptr;
static HWND gNoteUnderline = nullptr;
static HMODULE gRichEditModule = nullptr;
static bool gUnlocked = false;
static bool gSettingsOpen = false;

static SYSTEMTIME gCalendarMonth{};
static std::wstring gDataDir;
static std::wstring gNoteFile;
static std::wstring gConfigFile;

struct AppConfig {
    bool showLunar = true;
    bool showTop = true;
    bool autosave = true;
    int noteFontSize = 18;
    int opacity = 96;
    int theme = 0;
    COLORREF noteBg = RGB(255,255,255);
    COLORREF noteText = RGB(35,35,35);
    COLORREF accent = RGB(45,105,190);
    std::wstring password = L"1234";
};

static AppConfig gCfg;
// V2 settings state


static COLORREF BgColor() { return gCfg.theme ? RGB(32,34,37) : RGB(247,247,245); }
static COLORREF PanelColor() { return gCfg.theme ? RGB(45,47,51) : RGB(255,255,255); }
static COLORREF TextColor() { return gCfg.theme ? RGB(240,240,240) : RGB(35,35,35); }
static COLORREF MutedColor() { return gCfg.theme ? RGB(185,185,185) : RGB(100,100,100); }
static COLORREF AccentColor() { return gCfg.accent; }
static COLORREF CalendarSolarColor() { return gCfg.theme ? RGB(120,190,255) : RGB(40,105,190); }
static COLORREF CalendarLunarColor() { return gCfg.theme ? RGB(255,120,120) : RGB(205,55,55); }

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
    f << L"showLunar=" << (gCfg.showLunar ? 1 : 0) << L"\n";
    f << L"showTop=" << (gCfg.showTop ? 1 : 0) << L"\n";
    f << L"autosave=" << (gCfg.autosave ? 1 : 0) << L"\n";
    f << L"noteFontSize=" << gCfg.noteFontSize << L"\n";
    f << L"opacity=" << gCfg.opacity << L"\n";
    f << L"theme=" << gCfg.theme << L"\n";
    f << L"noteBg=" << (unsigned long)gCfg.noteBg << L"\n";
    f << L"noteText=" << (unsigned long)gCfg.noteText << L"\n";
    f << L"accent=" << (unsigned long)gCfg.accent << L"\n";
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
        if (k == L"showLunar") gCfg.showLunar = IntValue(v,1) != 0;
        else if (k == L"showTop") gCfg.showTop = IntValue(v,1) != 0;
        else if (k == L"autosave") gCfg.autosave = IntValue(v,1) != 0;
        else if (k == L"noteFontSize") gCfg.noteFontSize = std::clamp(IntValue(v,18),12,30);
        else if (k == L"opacity") gCfg.opacity = std::clamp(IntValue(v,96),55,100);
        else if (k == L"theme") gCfg.theme = std::clamp(IntValue(v,0),0,1);
        else if (k == L"noteBg") gCfg.noteBg = (COLORREF)IntValue(v,(int)RGB(255,255,255));
        else if (k == L"noteText") gCfg.noteText = (COLORREF)IntValue(v,(int)RGB(35,35,35));
        else if (k == L"accent") gCfg.accent = (COLORREF)IntValue(v,(int)RGB(45,105,190));
        else if (k == L"password" && !v.empty()) gCfg.password = v;
    }
}

struct RtfStream {
    std::string data;
    size_t pos=0;
};

static DWORD CALLBACK StreamOutProc(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    RtfStream* s=reinterpret_cast<RtfStream*>(cookie);
    size_t remain=s->data.size()-s->pos;
    size_t n=std::min<size_t>(remain, static_cast<size_t>(cb));
    if(n) memcpy(buf,s->data.data()+s->pos,n);
    s->pos+=n;
    *pcb=static_cast<LONG>(n);
    return 0;
}

static DWORD CALLBACK StreamInProc(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    RtfStream* s=reinterpret_cast<RtfStream*>(cookie);
    size_t remain=s->data.size()-s->pos;
    size_t n=std::min<size_t>(remain, static_cast<size_t>(cb));
    if(n) memcpy(buf,s->data.data()+s->pos,n);
    s->pos+=n;
    *pcb=static_cast<LONG>(n);
    return 0;
}

static void SaveNote() {
    if (!gNote) return;
    RtfStream s;
    EDITSTREAM es{};
    es.dwCookie=reinterpret_cast<DWORD_PTR>(&s);
    es.pfnCallback=StreamOutProc;
    SendMessageW(gNote,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&es));
    if(!s.data.empty()) {
        std::ofstream f(gNoteFile,std::ios::binary|std::ios::trunc);
        if(f) f.write(s.data.data(),static_cast<std::streamsize>(s.data.size()));
    }
}

static void LoadNote() {
    if (!gNote) return;
    std::ifstream f(gNoteFile,std::ios::binary);
    if(!f) return;
    std::string data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    if(data.empty()) return;
    RtfStream s; s.data=std::move(data);
    EDITSTREAM es{};
    es.dwCookie=reinterpret_cast<DWORD_PTR>(&s);
    es.pfnCallback=StreamInProc;
    SendMessageW(gNote,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&es));
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
static void RebuildNoteBrush() {
    if (gNoteBgBrush) { DeleteObject(gNoteBgBrush); gNoteBgBrush=nullptr; }
    gNoteBgBrush=CreateSolidBrush(gCfg.noteBg);
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
                HPEN tp=CreatePen(PS_SOLID,2,AccentColor());
                HGDIOBJ op=SelectObject(dc,tp);
                HGDIOBJ ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
                int cx=(cr.left+cr.right)/2;
                int cy=cr.top+13;
                int rad=14;
                Ellipse(dc,cx-rad,cy-rad,cx+rad,cy+rad);
                SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(tp);
            }
            wchar_t s[64]{};
            LunarDate ld=SolarToLunar(day,gCalendarMonth.wMonth,gCalendarMonth.wYear);
            swprintf_s(s,L"%d",day);
            SetTextColor(dc,CalendarSolarColor());
            DrawTextCenter(dc,s,RECT{cr.left,cr.top+2,cr.right,cr.top+24},DT_CENTER);
            if(gCfg.showLunar) {
                wchar_t ls[32]{};
                swprintf_s(ls,L"%d/%d",ld.day,ld.month);
                SetTextColor(dc,CalendarLunarColor());
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

static void PaintHeader(HWND h) {
    InvalidateRect(h,nullptr,TRUE);
    InvalidateRect(gCalendar,nullptr,TRUE);
}


static void SetNoteDefaultFormat() {
    if(!gNote) return;
    CHARRANGE all{-1, -1};
    SendMessageW(gNote,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
    CHARFORMAT2W cf{};
    cf.cbSize=sizeof(cf);
    cf.dwMask=CFM_FACE|CFM_SIZE|CFM_COLOR;
    cf.yHeight=gCfg.noteFontSize*20;
    cf.crTextColor=gCfg.noteText;
    wcscpy_s(cf.szFaceName,L"Segoe UI");
    SendMessageW(gNote,EM_SETCHARFORMAT,SCF_SELECTION|SPF_DONTSETDEFAULT,
                 reinterpret_cast<LPARAM>(&cf));
    SendMessageW(gNote,EM_SETBKGNDCOLOR,0,gCfg.noteBg);
}

static void ApplySelectionFormat(DWORD mask, DWORD effects=0, LONG yHeight=0, COLORREF color=0, const wchar_t* face=nullptr) {
    if(!gNote || !gUnlocked) return;
    CHARFORMAT2W cf{};
    cf.cbSize=sizeof(cf);
    cf.dwMask=mask;
    cf.dwEffects=effects;
    if(mask & CFM_SIZE) cf.yHeight=yHeight;
    if(mask & CFM_COLOR) cf.crTextColor=color;
    if(mask & CFM_FACE) wcscpy_s(cf.szFaceName,face?face:L"Segoe UI");
    SendMessageW(gNote,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&cf));
    SaveNote();
}

static bool PickNoteColor(HWND owner, COLORREF current, COLORREF& result) {
    static COLORREF custom[16]{};
    CHOOSECOLORW cc{};
    cc.lStructSize=sizeof(cc);
    cc.hwndOwner=owner;
    cc.rgbResult=current;
    cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(ChooseColorW(&cc)){result=cc.rgbResult;return true;}
    return false;
}

static void PopulateFontCombo() {
    if(!gNoteFontCombo) return;
    const wchar_t* fonts[]={L"Segoe UI",L"Arial",L"Calibri",L"Tahoma",L"Times New Roman",L"Verdana",L"Consolas"};
    for(auto f:fonts) SendMessageW(gNoteFontCombo,CB_ADDSTRING,0,(LPARAM)f);
    SendMessageW(gNoteFontCombo,CB_SETCURSEL,0,0);
}

static void ShowNoteToolbar(bool show) {
    if(!gNoteToolbar) return;
    ShowWindow(gNoteToolbar,show?SW_SHOW:SW_HIDE);
    if(show) {
        RECT r{}; GetClientRect(gMain,&r);
        int gap=20;
        int calW=std::max(330, static_cast<int>((r.right-gap)/2-10));
        int noteX=18+calW+gap;
        int noteW=std::max(280, static_cast<int>(r.right-noteX-18));
        MoveWindow(gNoteToolbar,noteX+10,116,noteW-20,32,TRUE);
    }
}

static void UpdateToolbarFromSelection() {
    if(!gNote || !gNoteToolbar) return;
    CHARFORMAT2W cf{};
    cf.cbSize=sizeof(cf);
    SendMessageW(gNote,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&cf));
    SendMessageW(gNoteBold,BM_SETCHECK,(cf.dwEffects&CFE_BOLD)?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(gNoteItalic,BM_SETCHECK,(cf.dwEffects&CFE_ITALIC)?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(gNoteUnderline,BM_SETCHECK,(cf.dwEffects&CFE_UNDERLINE)?BST_CHECKED:BST_UNCHECKED,0);
    int size=cf.yHeight/20;
    wchar_t b[16]{}; swprintf_s(b,L"%d",size>0?size:gCfg.noteFontSize);
    SetWindowTextW(gNoteSizeCombo,b);
}

static void CreateNoteToolbar(HWND h) {
    gNoteToolbar=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,
        0,0,500,32,h,(HMENU)IDC_NOTE_TOOLBAR,GetModuleHandleW(nullptr),nullptr);
    gNoteFontCombo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,
        6,3,125,26,gNoteToolbar,(HMENU)IDC_NOTE_FONT,GetModuleHandleW(nullptr),nullptr);
    PopulateFontCombo();
    gNoteSizeCombo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|CBS_AUTOHSCROLL,
        137,3,55,26,gNoteToolbar,(HMENU)IDC_NOTE_SIZE,GetModuleHandleW(nullptr),nullptr);
    const int sizes[]={10,11,12,14,16,18,20,22,24,28,32,36,48};
    for(int v:sizes){wchar_t b[8]{};swprintf_s(b,L"%d",v);SendMessageW(gNoteSizeCombo,CB_ADDSTRING,0,(LPARAM)b);}
    gNoteBold=CreateWindowW(L"BUTTON",L"B",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
        198,3,32,26,gNoteToolbar,(HMENU)IDC_NOTE_BOLD,GetModuleHandleW(nullptr),nullptr);
    gNoteItalic=CreateWindowW(L"BUTTON",L"I",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
        234,3,32,26,gNoteToolbar,(HMENU)IDC_NOTE_ITALIC,GetModuleHandleW(nullptr),nullptr);
    gNoteUnderline=CreateWindowW(L"BUTTON",L"U",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
        270,3,32,26,gNoteToolbar,(HMENU)IDC_NOTE_UNDER,GetModuleHandleW(nullptr),nullptr);
    CreateWindowW(L"BUTTON",L"Màu chữ",WS_CHILD|WS_VISIBLE,
        306,3,75,26,gNoteToolbar,(HMENU)IDC_NOTE_TEXTCOLOR,GetModuleHandleW(nullptr),nullptr);
    CreateWindowW(L"BUTTON",L"Nền",WS_CHILD|WS_VISIBLE,
        385,3,60,26,gNoteToolbar,(HMENU)IDC_NOTE_BGCOLOR,GetModuleHandleW(nullptr),nullptr);
    for(HWND c=GetWindow(gNoteToolbar,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) ApplyFont(c,gUiFont);
    ShowWindow(gNoteToolbar,SW_HIDE);
}

static void SetLocked(bool locked) {
    gUnlocked=!locked;
    EnableWindow(gNote,gUnlocked);
    EnableWindow(GetDlgItem(gMain,IDC_SETTINGS),gUnlocked);
    if(gNoteToolbar) {
        EnableWindow(gNoteToolbar,gUnlocked);
        for(HWND c=GetWindow(gNoteToolbar,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) EnableWindow(c,gUnlocked);
    }
    if(!gUnlocked) ShowNoteToolbar(false);

    // Locked = cannot move/resize/close. Unlock = normal window editing.
    LONG_PTR style=GetWindowLongPtrW(gMain,GWL_STYLE);
    if(gUnlocked) style|=WS_THICKFRAME|WS_MAXIMIZEBOX;
    else style&=~(WS_THICKFRAME|WS_MAXIMIZEBOX);
    SetWindowLongPtrW(gMain,GWL_STYLE,style);
    SetWindowPos(gMain,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);

    SetWindowTextW(GetDlgItem(gMain,IDC_UNLOCK),gUnlocked?L"Khóa lại":L"Mở khóa");
    SetWindowTextW(gStatus,gUnlocked?L"ĐÃ MỞ KHÓA":L"ĐANG KHÓA");
    ShowNoteToolbar(gUnlocked && GetFocus()==gNote);
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
    HWND tab=nullptr;
    HWND chkLunar=nullptr, chkTop=nullptr;
    HWND cmbTheme=nullptr;
    HWND edtFont=nullptr, edtOpacity=nullptr;
    HWND chkAutosave=nullptr;
    HWND edtPassword=nullptr;
    HWND btnNoteBg=nullptr, btnNoteText=nullptr, btnAccent=nullptr;
    COLORREF noteBg, noteText, accent;
    AppConfig original;
};
static SettingsState* gSet=nullptr;

static void AddLabel(HWND p, const wchar_t* s, int x, int y, int w, int h) {
    HWND c=CreateWindowW(L"STATIC",s,WS_CHILD|WS_VISIBLE,x,y,w,h,p,nullptr,GetModuleHandleW(nullptr),nullptr);
    ApplyFont(c,gUiFont);
}
static HWND AddCheck(HWND p,const wchar_t* s,int id,int x,int y,bool checked) {
    HWND c=CreateWindowW(L"BUTTON",s,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,300,28,p,(HMENU)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
    ApplyFont(c,gUiFont); return c;
}
static HWND AddCombo(HWND p,int id,int x,int y,const wchar_t** items,int count,int selected) {
    HWND c=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,x,y,210,30,p,(HMENU)id,GetModuleHandleW(nullptr),nullptr);
    for(int i=0;i<count;++i) SendMessageW(c,CB_ADDSTRING,0,(LPARAM)items[i]);
    SendMessageW(c,CB_SETCURSEL,selected,0); ApplyFont(c,gUiFont); return c;
}
static HWND AddEdit(HWND p,int id,int x,int y,const wchar_t* value,bool password=false) {
    DWORD st=WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL;
    HWND c=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",value,st|(password?ES_PASSWORD:0),x,y,100,28,p,(HMENU)id,GetModuleHandleW(nullptr),nullptr);
    ApplyFont(c,gUiFont); return c;
}


static bool PickColor(HWND owner, COLORREF current, COLORREF& result) {
    static COLORREF custom[16]{};
    CHOOSECOLORW cc{};
    cc.lStructSize=sizeof(cc);
    cc.hwndOwner=owner;
    cc.rgbResult=current;
    cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;
    if (ChooseColorW(&cc)) { result=cc.rgbResult; return true; }
    return false;
}
static void SetColorButtonText(HWND b, COLORREF c) {
    wchar_t s[64]{};
    swprintf_s(s,L"Màu  #%02X%02X%02X",GetRValue(c),GetGValue(c),GetBValue(c));
    SetWindowTextW(b,s);
}
static void SettingsApplyFromUI(SettingsState* s) {
    gCfg.theme=(int)SendMessageW(s->cmbTheme,CB_GETCURSEL,0,0);
    gCfg.showLunar=SendMessageW(s->chkLunar,BM_GETCHECK,0,0)==BST_CHECKED;
    gCfg.showTop=SendMessageW(s->chkTop,BM_GETCHECK,0,0)==BST_CHECKED;
    gCfg.autosave=SendMessageW(s->chkAutosave,BM_GETCHECK,0,0)==BST_CHECKED;
    gCfg.noteBg=s->noteBg;
    gCfg.noteText=s->noteText;
    gCfg.accent=s->accent;

    wchar_t b[64]{};
    GetWindowTextW(s->edtFont,b,64); gCfg.noteFontSize=std::clamp(IntValue(b,18),12,32);
    GetWindowTextW(s->edtOpacity,b,64); gCfg.opacity=std::clamp(IntValue(b,96),55,100);

    wchar_t np[256]{}; GetWindowTextW(s->edtPassword,np,256);
    if(np[0]) gCfg.password=np;

    CreateFonts();
    RebuildNoteBrush();
    ApplyFont(gNote,gNoteFont);
    SendMessageW(gNote,EM_SETBKGNDCOLOR,0,gCfg.noteBg);
    CHARRANGE all{-1,-1};
    SendMessageW(gNote,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
    CHARFORMAT2W cf{};
    cf.cbSize=sizeof(cf);
    cf.dwMask=CFM_SIZE|CFM_COLOR|CFM_FACE;
    cf.yHeight=gCfg.noteFontSize*20;
    cf.crTextColor=gCfg.noteText;
    wcscpy_s(cf.szFaceName,L"Segoe UI");
    SendMessageW(gNote,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&cf));
    SendMessageW(gNote,EM_SETSEL,-1,-1);
    ApplyFontsToChildren(gMain);
    ApplyOpacity();
    InvalidateRect(gNote,nullptr,TRUE);

    if(gCfg.showTop) SetWindowPos(gMain,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    else SetWindowPos(gMain,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);

    SaveConfig();
    PaintHeader(gMain);
    InvalidateRect(gMain,nullptr,TRUE);
    InvalidateRect(gCalendar,nullptr,TRUE);
}

static LRESULT CALLBACK SettingsProc(HWND h,UINT m,WPARAM wp,LPARAM lp) {
    SettingsState* s=(SettingsState*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE) {
        auto cs=(CREATESTRUCTW*)lp; s=(SettingsState*)cs->lpCreateParams;
        SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)s);
    }
    switch(m) {
    case WM_CREATE: {
        SetWindowTextW(h,L"Desktop Note - Cài đặt");

        // Header
        HWND hdr=CreateWindowW(L"STATIC",L"CÀI ĐẶT DESKTOP NOTE",
            WS_CHILD|WS_VISIBLE|SS_CENTER,20,12,600,32,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(hdr,gUiFont);

        // Group 1 - interface
        HWND g1=CreateWindowW(L"BUTTON",L"1. Giao diện",
            WS_CHILD|WS_VISIBLE|BS_GROUPBOX,20,52,600,150,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(g1,gUiFont);

        AddLabel(h,L"Chủ đề:",45,82,100,25);
        const wchar_t* themes[]={L"Sáng",L"Tối"};
        s->cmbTheme=AddCombo(h,ID_SET_THEME,145,78,themes,2,gCfg.theme);

        AddLabel(h,L"Độ trong suốt (%):",385,82,130,25);
        wchar_t b[32]{}; swprintf_s(b,L"%d",gCfg.opacity);
        s->edtOpacity=AddEdit(h,ID_SET_OPACITY,520,78,b);

        s->chkTop=AddCheck(h,L"Luôn hiển thị trên cùng",ID_SET_TOP,45,120,gCfg.showTop);
        s->chkAutosave=AddCheck(h,L"Tự động lưu ghi chú",ID_SET_AUTOSAVE,310,120,gCfg.autosave);

        // Group 2 - calendar
        HWND g2=CreateWindowW(L"BUTTON",L"2. Lịch Âm / Dương",
            WS_CHILD|WS_VISIBLE|BS_GROUPBOX,20,212,600,105,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(g2,gUiFont);

        s->chkLunar=AddCheck(h,L"Hiển thị lịch âm màu đỏ dưới ngày dương",ID_SET_LUNAR,45,242,gCfg.showLunar);
        AddLabel(h,L"Dương lịch: xanh biển  •  Âm lịch: đỏ",45,274,360,25);

        // Group 3 - note
        HWND g3=CreateWindowW(L"BUTTON",L"3. Giao diện ô Note",
            WS_CHILD|WS_VISIBLE|BS_GROUPBOX,20,327,600,145,h,nullptr,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(g3,gUiFont);

        AddLabel(h,L"Cỡ chữ Note:",45,358,110,25);
        swprintf_s(b,L"%d",gCfg.noteFontSize);
        s->edtFont=AddEdit(h,ID_SET_FONT,155,354,b);

        AddLabel(h,L"12 - 32 px",270,358,100,25);

        AddLabel(h,L"Màu nền Note:",45,390,120,25);
        s->btnNoteBg=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE,175,386,145,30,h,(HMENU)ID_SET_NOTE_BG,GetModuleHandleW(nullptr),nullptr);
        s->noteBg=gCfg.noteBg; SetColorButtonText(s->btnNoteBg,s->noteBg); ApplyFont(s->btnNoteBg,gUiFont);

        AddLabel(h,L"Màu chữ Note:",335,390,120,25);
        s->btnNoteText=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE,455,386,145,30,h,(HMENU)ID_SET_NOTE_TEXT,GetModuleHandleW(nullptr),nullptr);
        s->noteText=gCfg.noteText; SetColorButtonText(s->btnNoteText,s->noteText); ApplyFont(s->btnNoteText,gUiFont);

        AddLabel(h,L"Màu nhấn:",45,424,120,25);
        s->btnAccent=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE,175,420,145,30,h,(HMENU)ID_SET_ACCENT,GetModuleHandleW(nullptr),nullptr);
        s->accent=gCfg.accent; SetColorButtonText(s->btnAccent,s->accent); ApplyFont(s->btnAccent,gUiFont);

        // Password
        AddLabel(h,L"Mật khẩu mới (để trống nếu không đổi):",45,485,330,25);
        s->edtPassword=AddEdit(h,ID_SET_PASSWORD,380,481,L"",true);

        HWND save=CreateWindowW(L"BUTTON",L"LƯU THAY ĐỔI",
            WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,360,525,125,36,h,(HMENU)ID_SET_SAVE,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(save,gUiFont);

        HWND cancel=CreateWindowW(L"BUTTON",L"HỦY",
            WS_CHILD|WS_VISIBLE,495,525,100,36,h,(HMENU)ID_SET_CANCEL,GetModuleHandleW(nullptr),nullptr);
        ApplyFont(cancel,gUiFont);

        // Set font on group boxes and all remaining children.
        for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) ApplyFont(c,gUiFont);
        return 0;
    }

    case WM_COMMAND:
        if(LOWORD(wp)==ID_SET_NOTE_BG) {
            if(PickColor(h,s->noteBg,s->noteBg)) SetColorButtonText(s->btnNoteBg,s->noteBg);
            return 0;
        }
        if(LOWORD(wp)==ID_SET_NOTE_TEXT) {
            if(PickColor(h,s->noteText,s->noteText)) SetColorButtonText(s->btnNoteText,s->noteText);
            return 0;
        }
        if(LOWORD(wp)==ID_SET_ACCENT) {
            if(PickColor(h,s->accent,s->accent)) SetColorButtonText(s->btnAccent,s->accent);
            return 0;
        }
        if(LOWORD(wp)==ID_SET_SAVE) {
            SettingsApplyFromUI(s);
            DestroyWindow(h);
            gSettingsOpen=false; gSet=nullptr;
            return 0;
        }
        if(LOWORD(wp)==ID_SET_CANCEL) {
            DestroyWindow(h);
            gSettingsOpen=false; gSet=nullptr;
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(h); gSettingsOpen=false; gSet=nullptr; return 0;

    case WM_NCDESTROY:
        delete s; gSet=nullptr; break;
    }
    return DefWindowProcW(h,m,wp,lp);
}

static void OpenSettings(HWND owner) {
    if(!gUnlocked) {
        if(!AskPassword(owner)) return;
        SetLocked(false);
    }
    if(gSettingsOpen && gSet && IsWindow(gSet->wnd)) {
        SetForegroundWindow(gSet->wnd);
        return;
    }

    static bool reg=false;
    if(!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc=SettingsProc;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"DesktopNoteSettingsComplete";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        RegisterClassW(&wc);
        reg=true;
    }

    SettingsState* s=new SettingsState();
    s->original=gCfg;
    gSet=s;
    gSettingsOpen=true;

    HWND d=CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"DesktopNoteSettingsComplete",
        L"Desktop Note - Cài đặt",
        WS_CAPTION|WS_SYSMENU,
        0,0,660,585,
        owner,nullptr,GetModuleHandleW(nullptr),s);

    s->wnd=d;

    RECT dr{},orx{};
    GetWindowRect(d,&dr); GetWindowRect(owner,&orx);
    int x=orx.left+((orx.right-orx.left)-(dr.right-dr.left))/2;
    int y=orx.top+((orx.bottom-orx.top)-(dr.bottom-dr.top))/2;
    SetWindowPos(d,HWND_TOP,x,y,0,0,SWP_NOSIZE);

    ShowWindow(d,SW_SHOW);
    UpdateWindow(d);
}


static void AutoGrowForNote() {
    if(!gMain || !gNote || !gUnlocked) return;
    int lines=(int)SendMessageW(gNote,EM_GETLINECOUNT,0,0);
    if(lines<1) lines=1;
    // Estimate one line at current font size, plus toolbar and panel margins.
    int linePx=std::clamp(gCfg.noteFontSize+8,20,48);
    int desired=560+std::min(std::max(0,lines-4)*linePx,420);
    RECT wr{}; GetWindowRect(gMain,&wr);
    HMONITOR mon=MonitorFromWindow(gMain,MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon,&mi);
    int maxH=(mi.rcWork.bottom-mi.rcWork.top)*85/100;
    int minH=520;
    desired=std::clamp(desired,minH,maxH);
    int curH=wr.bottom-wr.top;
    if(lines>10 && desired>curH) {
        SetWindowPos(gMain,nullptr,wr.left,wr.top,wr.right-wr.left,desired,SWP_NOZORDER|SWP_NOACTIVATE);
    } else if(lines<=3 && curH>minH+20) {
        SetWindowPos(gMain,nullptr,wr.left,wr.top,wr.right-wr.left,minH,SWP_NOZORDER|SWP_NOACTIVATE);
    }
}

static LRESULT CALLBACK MainProc(HWND h,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_CREATE: {
        gMain=h;
        gDataDir=GetModuleDir();
        gNoteFile=gDataDir+L"\\DesktopNote_Note.rtf";
        gConfigFile=gDataDir+L"\\DesktopNote_Config.txt";
        LoadConfig();
        CreateFonts();
        TodayToCalendar();
        gRichEditModule=LoadLibraryW(L"Msftedit.dll");

        gCalendar=CreateWindowExW(0,L"DesktopNoteCalendar",L"",WS_CHILD|WS_VISIBLE,
            26,116,430,350,h,nullptr,GetModuleHandleW(nullptr),nullptr);

        gNote=CreateWindowExW(WS_EX_CLIENTEDGE,L"RICHEDIT50W",L"",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL|WS_HSCROLL,
            476,155,430,310,h,(HMENU)IDC_NOTE,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(gNote,EM_SETOPTIONS,0,SendMessageW(gNote,EM_GETOPTIONS,0,0)|ECOOP_OR|ECO_AUTOVSCROLL|ECO_AUTOHSCROLL);
        SendMessageW(gNote,EM_SETEVENTMASK,0,ENM_SELCHANGE|ENM_CHANGE|ENM_DROPFILES);

        gStatus=CreateWindowW(L"STATIC",L"ĐANG KHÓA",WS_CHILD|WS_VISIBLE,
            18,425,300,28,h,nullptr,GetModuleHandleW(nullptr),nullptr);

        CreateWindowW(L"BUTTON",L"Mở khóa",WS_CHILD|WS_VISIBLE,650,420,105,34,h,(HMENU)IDC_UNLOCK,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Cài đặt",WS_CHILD|WS_VISIBLE,765,420,105,34,h,(HMENU)IDC_SETTINGS,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Khóa",WS_CHILD|WS_VISIBLE,535,420,105,34,h,(HMENU)IDC_LOCK,GetModuleHandleW(nullptr),nullptr);

        CreateNoteToolbar(h);
        ShowNoteToolbar(false);
        LoadNote();
        RebuildNoteBrush();
        SendMessageW(gNote,EM_SETBKGNDCOLOR,0,gCfg.noteBg);
        if(GetWindowTextLengthW(gNote)==0) SetNoteDefaultFormat();
        else SendMessageW(gNote,EM_SETSEL,-1,-1);
        ApplyFontsToChildren(h);
        SetNoteDefaultFormat();
        SetLocked(true);
        if(gCfg.showTop) SetWindowPos(gMain,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        ApplyOpacity();
        SetTimer(h,IDT_AUTOSAVE,3000,nullptr);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mi=reinterpret_cast<MINMAXINFO*>(lp);
        mi->ptMinTrackSize.x=760;
        mi->ptMinTrackSize.y=520;
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
            else if(AskPassword(h)) { SetLocked(false); AutoGrowForNote(); }
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
        if(LOWORD(wp)==IDC_NOTE && HIWORD(wp)==EN_SETFOCUS && gUnlocked) {
            ShowNoteToolbar(true);
            UpdateToolbarFromSelection();
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE && HIWORD(wp)==EN_KILLFOCUS) {
            SetTimer(h,IDT_TOOLBAR,1200,nullptr);
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE && HIWORD(wp)==EN_CHANGE && gUnlocked) {
            AutoGrowForNote();
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_FONT) {
            int idx=(int)SendMessageW(gNoteFontCombo,CB_GETCURSEL,0,0);
            if(idx>=0) {
                wchar_t face[64]{};
                SendMessageW(gNoteFontCombo,CB_GETLBTEXT,idx,(LPARAM)face);
                ApplySelectionFormat(CFM_FACE,0,0,0,face);
            }
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_SIZE && (HIWORD(wp)==CBN_SELCHANGE || HIWORD(wp)==CBN_EDITCHANGE)) {
            wchar_t b[32]{}; GetWindowTextW(gNoteSizeCombo,b,32);
            int v=std::clamp(IntValue(b,gCfg.noteFontSize),8,72);
            ApplySelectionFormat(CFM_SIZE,0,v*20);
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_BOLD) {
            bool on=SendMessageW(gNoteBold,BM_GETCHECK,0,0)==BST_CHECKED;
            ApplySelectionFormat(CFM_BOLD,on?CFE_BOLD:0);
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_ITALIC) {
            bool on=SendMessageW(gNoteItalic,BM_GETCHECK,0,0)==BST_CHECKED;
            ApplySelectionFormat(CFM_ITALIC,on?CFE_ITALIC:0);
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_UNDER) {
            bool on=SendMessageW(gNoteUnderline,BM_GETCHECK,0,0)==BST_CHECKED;
            ApplySelectionFormat(CFM_UNDERLINE,on?CFE_UNDERLINE:0);
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_TEXTCOLOR) {
            COLORREF c=gCfg.noteText;
            if(PickNoteColor(h,c,c)) {
                ApplySelectionFormat(CFM_COLOR,0,0,c);
            }
            return 0;
        }
        if(LOWORD(wp)==IDC_NOTE_BGCOLOR) {
            COLORREF c=gCfg.noteBg;
            if(PickNoteColor(h,c,c)) {
                gCfg.noteBg=c; RebuildNoteBrush();
                SendMessageW(gNote,EM_SETBKGNDCOLOR,0,c);
                InvalidateRect(gNote,nullptr,TRUE);
                SaveConfig();
            }
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT:
        if((HWND)lp==gNote) {
            HDC dc=(HDC)wp;
            SetTextColor(dc,gCfg.noteText);
            SetBkColor(dc,gCfg.noteBg);
            return (LRESULT)gNoteBgBrush;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)wp;
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,TextColor());
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_NOTIFY: {
        NMHDR* nh=reinterpret_cast<NMHDR*>(lp);
        if(nh && nh->hwndFrom==gNote && nh->code==EN_SELCHANGE) {
            UpdateToolbarFromSelection();
            return 0;
        }
        break;
    }

    case WM_SETFOCUS:
        if(gUnlocked) ShowNoteToolbar(true);
        break;

    case WM_TIMER:
        if(wp==IDT_AUTOSAVE && gUnlocked && gCfg.autosave) SaveNote();
        else if(wp==IDT_TOOLBAR) { KillTimer(h,IDT_TOOLBAR); if(GetFocus()!=gNote) ShowNoteToolbar(false); }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(h,&ps);
        RECT r{}; GetClientRect(h,&r);
        HBRUSH bg=CreateSolidBrush(BgColor()); FillRect(dc,&r,bg); DeleteObject(bg);
        SetBkMode(dc,TRANSPARENT);

        // Header
        HBRUSH header=CreateSolidBrush(gCfg.theme?RGB(39,42,47):RGB(236,241,247));
        RECT hr{0,0,r.right,64}; FillRect(dc,&hr,header); DeleteObject(header);

        HFONT old=(HFONT)SelectObject(dc,gUiFont);
        SetTextColor(dc,TextColor());
        DrawTextW(dc,L"DESKTOP NOTE",-1,&RECT{22,12,360,45},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t date[128]{};
        swprintf_s(date,L"%s  •  %02d/%02d/%04d",WeekdayVN(st.wDayOfWeek).c_str(),st.wDay,st.wMonth,st.wYear);
        SetTextColor(dc,MutedColor());
        DrawTextW(dc,date,-1,&RECT{570,12,920,45},DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

        // Two framed areas: calendar and note.
        int gap=20;
        int calW=std::max(330, static_cast<int>((r.right-gap)/2-10));
        int calBoxR=18+calW;
        int noteL=calBoxR+gap;
        RECT calBox{18,78,calBoxR,r.bottom-75};
        RECT noteBox{noteL,78,r.right-18,r.bottom-75};

        HPEN pen=CreatePen(PS_SOLID,1,gCfg.theme?RGB(80,85,92):RGB(205,211,218));
        HGDIOBJ oldPen=SelectObject(dc,pen);
        HBRUSH nullBrush=(HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ oldBrush=SelectObject(dc,nullBrush);
        RoundRect(dc,calBox.left,calBox.top,calBox.right,calBox.bottom,12,12);
        RoundRect(dc,noteBox.left,noteBox.top,noteBox.right,noteBox.bottom,12,12);
        SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);

        SetTextColor(dc,AccentColor());
        DrawTextW(dc,L"LỊCH ÂM / DƯƠNG",-1,&RECT{34,84,250,110},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(dc,CalendarSolarColor());
        DrawTextW(dc,L"● Dương",-1,&RECT{250,84,325,110},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(dc,CalendarLunarColor());
        DrawTextW(dc,L"● Âm",-1,&RECT{325,84,390,110},DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        SetTextColor(dc,AccentColor());
        DrawTextW(dc,L"GHI CHÚ",-1,&RECT{noteL+16,84,noteL+150,110},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(dc,MutedColor());
        DrawTextW(dc,L"Tự động lưu khi đang mở khóa",-1,&RECT{noteL+180,84,r.right-32,110},DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

        // The actual note editor and toolbar are child windows inside this frame.
        SelectObject(dc,old);
        EndPaint(h,&ps);
        return 0;
    }

    case WM_SIZE: {
        int w=LOWORD(lp), hh=HIWORD(lp);
        int contentH=std::max(220, static_cast<int>(hh-153));
        int gap=20;
        int calW=std::max(330, static_cast<int>((w-gap)/2-10));
        int calX=18;
        int calY=116;
        int calH=std::max(180, static_cast<int>(hh-191));
        int noteX=calX+calW+gap;
        int noteW=std::max(280, static_cast<int>(w-noteX-18));

        MoveWindow(gCalendar,calX+8,calY,calW-16,calH,TRUE);
        MoveWindow(gNoteToolbar,noteX+10,116,noteW-20,32,TRUE);
        MoveWindow(gNote,noteX+10,154,noteW-20,std::max(100, static_cast<int>(hh-154-78)),TRUE);

        MoveWindow(gStatus,26,hh-52,300,28,TRUE);
        int bx=std::max(noteX, w-365);
        MoveWindow(GetDlgItem(h,IDC_LOCK),bx,hh-60,105,34,TRUE);
        MoveWindow(GetDlgItem(h,IDC_UNLOCK),bx+115,hh-60,105,34,TRUE);
        MoveWindow(GetDlgItem(h,IDC_SETTINGS),bx+230,hh-60,105,34,TRUE);
        InvalidateRect(h,nullptr,TRUE);
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
        KillTimer(h,IDT_AUTOSAVE);
        if(gNoteFont) DeleteObject(gNoteFont);
        if(gUiFont) DeleteObject(gUiFont);
        if(gNoteBgBrush) DeleteObject(gNoteBgBrush);
        if(gRichEditModule) { FreeLibrary(gRichEditModule); gRichEditModule=nullptr; }
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
        100,100,960,600,nullptr,nullptr,hInst,nullptr);

    if(!h) return 1;
    ShowWindow(h,nCmdShow); UpdateWindow(h);

    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
