#define _WIN32_WINNT 0x0A00 

#include <windows.h>
#include <shellapi.h> 
#include <wininet.h> 
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TIMER_CHANGE_PAGE 1
#define TIMER_ANIMATION   2
#define PI                3.14159265358979323846
#define WM_TRAYICON       (WM_USER + 1)
#define ID_TRAY_ICON      1
#define REG_KEY_PATH      L"Software\\LiveTileNewsDesktop"

// Dimensions et Échelle
int g_tileWidth = 310;
int g_tileHeight = 150;       
double g_scale = 1.0;

// Animation et État
int is_animating = 0;
int anim_frame = 0;         
int max_frames = 15;        
int g_animDir = 0; // 0: Haut, 1: Gauche, 2: Bas, 3: Droite

int is_dragging = 0;
POINT drag_start;
RECT window_start;

// Gestion du contenu (12 actus tournantes)
wchar_t news_pool[12][256];
int g_newsCount = 0;
int g_currentIndex = 0; 

// Nettoyeur de caractères HTML (ex: &quot; -> ")
void CleanHtml(wchar_t* s) {
    struct { wchar_t* code; wchar_t val; } table[] = { {L"&quot;", L'\"'}, {L"&apos;", L'\''}, {L"&amp;", L'&'}, {L"&lt;", L'<'}, {L"&gt;", L'>'}, {L"&nbsp;", L' '} };
    for(int i=0; i<6; i++) {
        wchar_t* p;
        while((p = wcsstr(s, table[i].code))) {
            *p = table[i].val;
            memmove(p+1, p+wcslen(table[i].code), (wcslen(p+wcslen(table[i].code))+1)*sizeof(wchar_t));
        }
    }
}

DWORD WINAPI FetchNewsThread(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;
    HINTERNET hInternet = InternetOpenW(L"LiveTileNews", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return 0;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, L"https://global.techradar.com/fr-fr/rss", NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return 0; }

    char* data = (char*)malloc(128000);
    DWORD totalRead = 0, bytesRead = 0;
    while (InternetReadFile(hUrl, data + totalRead, 127999 - totalRead, &bytesRead) && bytesRead > 0) {
        totalRead += bytesRead;
    }
    data[totalRead] = '\0';

    char* ptr = data;
    int count = 0;
    while (count < 12) {
        ptr = strstr(ptr, "<item>"); if (!ptr) break;
        ptr = strstr(ptr, "<title>"); if (!ptr) break;
        ptr += 7; 
        char* endPtr = strstr(ptr, "</title>"); if (!endPtr) break;
        
        int len = endPtr - ptr;
        char temp[512]; strncpy(temp, ptr, (len > 511) ? 511 : len); temp[len] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, temp, -1, news_pool[count], 256);
        
        // Nettoyage CDATA et HTML
        wchar_t* cdata = wcsstr(news_pool[count], L"<![CDATA[");
        if (cdata) {
            memmove(news_pool[count], cdata+9, (wcslen(cdata+9)+1)*sizeof(wchar_t));
            wchar_t* endC = wcsstr(news_pool[count], L"]]>"); if (endC) *endC = L'\0';
        }
        CleanHtml(news_pool[count]);
        count++; ptr = endPtr;
    }
    g_newsCount = count;
    free(data); InternetCloseHandle(hUrl); InternetCloseHandle(hInternet);
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
}

void DrawPageContent(HDC hdc, int startIndex, double shade) {
    COLORREF bgColor = RGB((int)(190 * shade), (int)(25 * shade), (int)(25 * shade)); 
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    RECT rectTile = {0, 0, g_tileWidth, g_tileHeight};
    FillRect(hdc, &rectTile, hBrush);
    DeleteObject(hBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB((int)(255*shade), (int)(255*shade), (int)(255*shade)));

    int pad = (int)(12 * g_scale);
    HFONT hFontHead = CreateFontW((int)(10 * g_scale), 0, 0, 0, FW_BOLD, 0,0,0, 0,0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT hFontNews = CreateFontW((int)(13 * g_scale), 0, 0, 0, FW_MEDIUM, 0,0,0, 0,0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    SelectObject(hdc, hFontHead);
    RECT rH = {pad, pad/2, g_tileWidth-pad, pad*2};
    DrawTextW(hdc, L"MSN TECH • À LA UNE", -1, &rH, DT_LEFT);

    SelectObject(hdc, hFontNews);
    for(int i=0; i<3; i++) {
        int idx = (startIndex + i) % (g_newsCount > 0 ? g_newsCount : 1);
        RECT rN = {pad, pad*2 + i*((g_tileHeight-pad*3)/3), g_tileWidth-pad, pad*2 + (i+1)*((g_tileHeight-pad*3)/3)};
        if (g_newsCount > 0) DrawTextW(hdc, news_pool[idx], -1, &rN, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    }

    DeleteObject(hFontHead); DeleteObject(hFontNews);
}

void ShowContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1003, L"Afficher");
    AppendMenuW(hMenu, MF_STRING, 1001, L"Masquer");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1002, L"Quitter");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int id = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    if (id == 1001) ShowWindow(hwnd, SW_HIDE);
    else if (id == 1003) {
        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    else if (id == 1002) DestroyWindow(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            FetchNewsThread(hwnd);
            SetTimer(hwnd, TIMER_CHANGE_PAGE, 8000, NULL); 

            // Creation de l'icone de la barre des tâches 📌
            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Charge l'icône par défaut de Windows
            wcscpy(nid.szTip, L"MSN Tech Live Tile");
            Shell_NotifyIconW(NIM_ADD, &nid);
            return 0;
        }
        
        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) ShowContextMenu(hwnd); 
            return 0;
        }

        case WM_LBUTTONDOWN: is_dragging = 1; SetCapture(hwnd); GetCursorPos(&drag_start); GetWindowRect(hwnd, &window_start); return 0;
        case WM_MOUSEMOVE: if(is_dragging){ POINT pt; GetCursorPos(&pt); SetWindowPos(hwnd, HWND_BOTTOM, window_start.left+(pt.x-drag_start.x), window_start.top+(pt.y-drag_start.y), 0, 0, SWP_NOSIZE | SWP_NOACTIVATE); } return 0;
        case WM_LBUTTONUP: if(is_dragging){ is_dragging=0; ReleaseCapture(); RECT r; GetWindowRect(hwnd,&r); HKEY h; if(RegCreateKeyExW(HKEY_CURRENT_USER,REG_KEY_PATH,0,0,0,KEY_WRITE,0,&h,0)==ERROR_SUCCESS){ RegSetValueExW(h,L"TileX",0,REG_DWORD,(BYTE*)&r.left,4); RegSetValueExW(h,L"TileY",0,REG_DWORD,(BYTE*)&r.top,4); RegCloseKey(h); } } return 0;
        case WM_RBUTTONUP: ShowContextMenu(hwnd); return 0;
        
        case WM_TIMER:
            if (wParam == TIMER_CHANGE_PAGE) {
                is_animating = 1; anim_frame = 0;
                g_animDir = (g_animDir + 1) % 4; 
                CreateThread(NULL, 0, FetchNewsThread, hwnd, 0, NULL);
                KillTimer(hwnd, TIMER_CHANGE_PAGE); SetTimer(hwnd, TIMER_ANIMATION, 16, NULL);
            }
            if (wParam == TIMER_ANIMATION) {
                anim_frame++;
                if (anim_frame >= max_frames) {
                    KillTimer(hwnd, TIMER_ANIMATION); is_animating = 0;
                    g_currentIndex = (g_currentIndex + 3) % (g_newsCount > 0 ? g_newsCount : 1);
                    SetTimer(hwnd, TIMER_CHANGE_PAGE, 8000, NULL);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            HDC hdcM = CreateCompatibleDC(hdc); HBITMAP hbmM = CreateCompatibleBitmap(hdc, g_tileWidth, g_tileHeight); SelectObject(hdcM, hbmM);
            HDC hdcP = CreateCompatibleDC(hdc); HBITMAP hbmP = CreateCompatibleBitmap(hdc, g_tileWidth, g_tileHeight); SelectObject(hdcP, hbmP);
            SetStretchBltMode(hdcM, HALFTONE);

            if (!is_animating) DrawPageContent(hdcM, g_currentIndex, 1.0);
            else {
                double p = (double)anim_frame/max_frames; double s1 = cos(p*PI/2), s2 = sin(p*PI/2);
                int nextIdx = (g_currentIndex + 3) % (g_newsCount > 0 ? g_newsCount : 1);
                
                if(g_animDir % 2 == 0) { 
                    int h1 = (int)(g_tileHeight*(1-p)), h2 = g_tileHeight-h1;
                    DrawPageContent(hdcP, g_currentIndex, s1);
                    StretchBlt(hdcM, 0, (g_animDir==2?h2:0), g_tileWidth, h1, hdcP, 0, 0, g_tileWidth, g_tileHeight, SRCCOPY);
                    DrawPageContent(hdcP, nextIdx, s2);
                    StretchBlt(hdcM, 0, (g_animDir==2?0:h1), g_tileWidth, h2, hdcP, 0, 0, g_tileWidth, g_tileHeight, SRCCOPY);
                } else { 
                    int w1 = (int)(g_tileWidth*(1-p)), w2 = w1; // Correction mineure largeur
                    int w2_draw = g_tileWidth - w1;
                    DrawPageContent(hdcP, g_currentIndex, s1);
                    StretchBlt(hdcM, (g_animDir==3?w2_draw:0), 0, w1, g_tileHeight, hdcP, 0, 0, g_tileWidth, g_tileHeight, SRCCOPY);
                    DrawPageContent(hdcP, nextIdx, s2);
                    StretchBlt(hdcM, (g_animDir==3?0:w1), 0, w2_draw, g_tileHeight, hdcP, 0, 0, g_tileWidth, g_tileHeight, SRCCOPY);
                }
            }
            BitBlt(hdc, 0, 0, g_tileWidth, g_tileHeight, hdcM, 0, 0, SRCCOPY);
            DeleteDC(hdcP); DeleteObject(hbmP); DeleteDC(hdcM); DeleteObject(hbmM);
            EndPaint(hwnd, &ps); return 0;
        }

        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            wp->hwndInsertAfter = HWND_BOTTOM; 
            wp->flags &= ~SWP_NOZORDER;
            return 0;
        }

        case WM_MOUSEACTIVATE: 
            return MA_NOACTIVATE;

        case WM_DESTROY: {
            // Nettoyage de l'icône du tray quand on quitte 🧼
            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_ICON;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0); 
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HDC hdcS = GetDC(NULL); g_scale = (double)GetDeviceCaps(hdcS, LOGPIXELSX) / 96.0; ReleaseDC(NULL, hdcS);
    g_tileWidth = (int)(310 * g_scale); g_tileHeight = (int)(150 * g_scale);

    int pX = 300, pY = 300;
    HKEY hK; if(RegOpenKeyExW(HKEY_CURRENT_USER,REG_KEY_PATH,0,KEY_READ,&hK)==ERROR_SUCCESS){ DWORD x,y,s=4; RegQueryValueExW(hK,L"TileX",0,0,(BYTE*)&x,&s); RegQueryValueExW(hK,L"TileY",0,0,(BYTE*)&y,&s); pX=x; pY=y; RegCloseKey(hK); }

    WNDCLASSW wc = {0}; wc.lpfnWndProc = WindowProc; wc.hInstance = hInst; wc.lpszClassName = L"LiveNewsTile";
    RegisterClassW(&wc);
    
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"LiveNewsTile", L"News", WS_POPUP | WS_VISIBLE, pX, pY, g_tileWidth, g_tileHeight, 0, 0, hInst, 0);

    MSG msg; while (GetMessage(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}