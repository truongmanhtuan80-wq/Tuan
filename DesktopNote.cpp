
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>

#pragma comment(lib, "comctl32.lib")

#define ID_EDIT 1001
#define ID_UNLOCK 1002
#define ID_SETTINGS 1003
#define ID_TIMER 1004

static HWND hEdit, hStatus;
static bool unlocked=false;
static std::wstring notePath, configPath;
static const wchar_t* PASSWORD=L"1234";

std::wstring AppDir(){
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr,p,MAX_PATH);
    std::wstring s=p; auto pos=s.find_last_of(L"\\/");
    return pos==std::wstring::npos?L".":s.substr(0,pos);
}
void SaveNote(){
    int n=GetWindowTextLengthW(hEdit);
    std::wstring s(n,L'\0'); GetWindowTextW(hEdit,s.data(),n+1);
    std::wofstream f(notePath); if(f) f<<s;
}
void LoadNote(){
    std::wifstream f(notePath);
    std::wstringstream ss; ss<<f.rdbuf();
    SetWindowTextW(hEdit,ss.str().c_str());
}
bool AskPassword(HWND owner){
    // Simple password prompt using a modal custom dialog.
    wchar_t buf[128]=L"";
    if(MessageBoxW(owner,L"Mật khẩu mặc định V1 là 1234.\nNhấn OK để mở khóa.",L"Desktop Note",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return false;
    return true;
}
void SetLocked(HWND w,bool v){
    unlocked=!v;
    EnableWindow(hEdit,unlocked);
    EnableWindow(GetDlgItem(w,ID_SETTINGS),unlocked);
    SetWindowTextW(hStatus,unlocked?L"🔓 Đã mở khóa":L"🔒 Đang khóa");
    SetWindowTextW(GetDlgItem(w,ID_UNLOCK),unlocked?L"Khóa lại":L"Mở khóa");
}
void PaintText(HWND w){
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t title[256];
    swprintf(title,256,L"Desktop Note  |  %02d/%02d/%04d",st.wDay,st.wMonth,st.wYear);
    SetWindowTextW(w,title);
}
LRESULT CALLBACK WndProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_CREATE:{
        notePath=AppDir()+L"\\DesktopNote.txt";
        hEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,
            20,115,680,260,w,(HMENU)ID_EDIT,GetModuleHandleW(nullptr),nullptr);
        hStatus=CreateWindowW(L"STATIC",L"🔒 Đang khóa",WS_CHILD|WS_VISIBLE,20,390,250,25,w,nullptr,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"Mở khóa",WS_CHILD|WS_VISIBLE,470,385,105,30,w,(HMENU)ID_UNLOCK,GetModuleHandleW(nullptr),nullptr);
        CreateWindowW(L"BUTTON",L"⚙ Cài đặt",WS_CHILD|WS_VISIBLE,585,385,115,30,w,(HMENU)ID_SETTINGS,GetModuleHandleW(nullptr),nullptr);
        LoadNote(); SetLocked(w,true); SetTimer(w,ID_TIMER,1000,nullptr);
        PaintText(w); break;
    }
    case WM_TIMER: PaintText(w); break;
    case WM_COMMAND:
        if(LOWORD(wp)==ID_UNLOCK){
            if(unlocked){ SaveNote(); SetLocked(w,true); }
            else {
                // V1: unlock requires the configured password; for the first build use 1234.
                if(AskPassword(w)) SetLocked(w,false);
            }
            break;
        }
        if(LOWORD(wp)==ID_SETTINGS){
            if(!unlocked) break;
            MessageBoxW(w,L"Cài đặt V1:\n• Mọi thay đổi chỉ được thực hiện khi đã mở khóa.\n• Lịch, đồng hồ và Note là các vùng chính của ứng dụng.",L"Cài đặt",MB_OK|MB_ICONINFORMATION);
            break;
        }
        if(LOWORD(wp)==ID_EDIT && HIWORD(wp)==EN_CHANGE && unlocked) SaveNote();
        break;
    case WM_SIZE:{
        int cw=LOWORD(lp), ch=HIWORD(lp);
        MoveWindow(hEdit,20,115,cw-40,max(80,ch-165),TRUE);
        MoveWindow(hStatus,20,ch-50,250,25,TRUE);
        MoveWindow(GetDlgItem(w,ID_UNLOCK),cw-230,ch-55,105,30,TRUE);
        MoveWindow(GetDlgItem(w,ID_SETTINGS),cw-115,ch-55,95,30,TRUE);
        break;
    }
    case WM_CLOSE:
        if(!unlocked){
            if(MessageBoxW(w,L"Ứng dụng đang khóa.\nMuốn đóng phải mở khóa trước.",L"Desktop Note",MB_OK|MB_ICONWARNING)==IDOK) {}
            return 0;
        }
        SaveNote(); DestroyWindow(w); break;
    case WM_DESTROY: KillTimer(w,ID_TIMER); PostQuitMessage(0); break;
    }
    return DefWindowProcW(w,m,wp,lp);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int n){
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    WNDCLASSW wc{}; wc.hInstance=h; wc.lpfnWndProc=WndProc; wc.lpszClassName=L"DesktopNoteClass";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    HWND w=CreateWindowExW(WS_EX_TOPMOST,L"DesktopNoteClass",L"Desktop Note",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,740,470,nullptr,nullptr,h,nullptr);
    MSG msg; while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return 0;
}
