#define _WIN32_WINNT 0x0A00 

#include <windows.h>
#include <shellapi.h> 
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TIMER_CHANGE_PAGE 1
#define TIMER_ANIMATION   2
#define PI                3.14159265358979323846

#define WM_TRAYICON       (WM_USER + 1)
#define ID_TRAY_ICON      1

// Clé de registre pour sauvegarder la position de la tuile 🔑
#define REG_KEY_PATH L"Software\\LiveTileWordDesktop"

int g_tileSize = 150;       
double g_scale = 1.0;

int current_page = 0;       
int is_animating = 0;
int anim_frame = 0;         
int max_frames = 12;        

int is_dragging = 0;
POINT drag_start;
RECT window_start;

wchar_t recent_files[5][256];

typedef struct {
    wchar_t name[256];
    FILETIME time;
} RecentFileInfo;

int CompareFileTimes(const void* a, const void* b) {
    return CompareFileTime(&((RecentFileInfo*)b)->time, &((RecentFileInfo*)a)->time);
}

void CleanCloudFileName(wchar_t* dest, const wchar_t* src) {
    int s = 0, d = 0;
    while (src[s] != L'\0' && d < 255) {
        if (src[s] == L'?') {
            break;
        }
        if (src[s] == L'%' && src[s+1] != L'\0' && src[s+2] != L'\0' && src[s+1] == L'2' && src[s+2] == L'0') {
            dest[d++] = L' ';
            s += 3;
        } else {
            dest[d++] = src[s++];
        }
    }
    dest[d] = L'\0';
}

void RefreshRecentWordFiles() {
    for (int i = 0; i < 5; i++) {
        wcscpy(recent_files[i], L"Aucun document");
    }

    RecentFileInfo temp_files[500]; 
    int count = 0;

    HKEY hKeyRoot;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Office\\16.0\\Word\\User MRU", 0, KEY_READ, &hKeyRoot) == ERROR_SUCCESS) {
        DWORD subKeyCount = 0;
        DWORD maxSubKeyLen = 0;
        RegQueryInfoKeyW(hKeyRoot, NULL, NULL, NULL, &subKeyCount, &maxSubKeyLen, NULL, NULL, NULL, NULL, NULL, NULL);
        
        wchar_t subKeyName[256];
        for (DWORD i = 0; i < subKeyCount; i++) {
            DWORD len = 256;
            if (RegEnumKeyExW(hKeyRoot, i, subKeyName, &len, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                
                wchar_t mruPath[512];
                wsprintfW(mruPath, L"Software\\Microsoft\\Office\\16.0\\Word\\User MRU\\%s\\File MRU", subKeyName);
                
                HKEY hKeyMru;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, mruPath, 0, KEY_READ, &hKeyMru) == ERROR_SUCCESS) {
                    for (int j = 1; j <= 30; j++) {
                        wchar_t valueName[32];
                        wsprintfW(valueName, L"Item %d", j);
                        
                        wchar_t valueData[2048]; 
                        DWORD dataSize = sizeof(valueData) - sizeof(wchar_t);
                        ZeroMemory(valueData, sizeof(valueData));
                        
                        if (RegQueryValueExW(hKeyMru, valueName, NULL, NULL, (BYTE*)valueData, &dataSize) == ERROR_SUCCESS) {
                            valueData[2047] = L'\0'; 
                            wchar_t* pathStart = wcschr(valueData, L'*');
                            if (pathStart) {
                                pathStart++;
                                wchar_t* fileName = wcsrchr(pathStart, L'\\');
                                if (!fileName) fileName = wcsrchr(pathStart, L'/');
                                if (!fileName) fileName = pathStart;
                                else fileName++;
                                
                                wchar_t cleanName[256];
                                CleanCloudFileName(cleanName, fileName);
                                
                                BOOL dup = FALSE;
                                for (int k = 0; k < count; k++) {
                                    if (_wcsicmp(temp_files[k].name, cleanName) == 0) {
                                        dup = TRUE; break;
                                    }
                                }
                                
                                if (!dup && count < 500 && wcslen(cleanName) > 0) {
                                    wcsncpy(temp_files[count].name, cleanName, 255);
                                    temp_files[count].name[255] = L'\0';
                                    
                                    wchar_t* tPtr = wcsstr(valueData, L"T01D");
                                    if (!tPtr) tPtr = wcsstr(valueData, L"t01d");
                                    ULONGLONG ftVal = 0;
                                    if (tPtr) {
                                        wchar_t hexStr[17];
                                        wcsncpy(hexStr, tPtr + 1, 16);
                                        hexStr[16] = L'\0';
                                        ftVal = _wcstoui64(hexStr, NULL, 16);
                                    }
                                    if (ftVal == 0) ftVal = 100 - j;
                                    
                                    temp_files[count].time.dwHighDateTime = (DWORD)(ftVal >> 32);
                                    temp_files[count].time.dwLowDateTime = (DWORD)(ftVal & 0xFFFFFFFF);
                                    count++;
                                }
                            }
                        }
                    }
                    RegCloseKey(hKeyMru);
                }

                wchar_t serverMruPath[512];
                wsprintfW(serverMruPath, L"Software\\Microsoft\\Office\\16.0\\Word\\User MRU\\%s\\Server File MRU", subKeyName);
                
                HKEY hKeyServerMru;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, serverMruPath, 0, KEY_READ, &hKeyServerMru) == ERROR_SUCCESS) {
                    for (int j = 1; j <= 30; j++) {
                        wchar_t valueName[32];
                        wsprintfW(valueName, L"Item %d", j);
                        
                        wchar_t valueData[2048]; 
                        DWORD dataSize = sizeof(valueData) - sizeof(wchar_t);
                        ZeroMemory(valueData, sizeof(valueData));
                        
                        if (RegQueryValueExW(hKeyServerMru, valueName, NULL, NULL, (BYTE*)valueData, &dataSize) == ERROR_SUCCESS) {
                            valueData[2047] = L'\0';
                            wchar_t* pathStart = wcschr(valueData, L'*');
                            if (!pathStart) pathStart = valueData;
                            else pathStart++;
                            
                            wchar_t* fileName = wcsrchr(pathStart, L'\\');
                            if (!fileName) fileName = wcsrchr(pathStart, L'/'); 
                            if (!fileName) fileName = pathStart;
                            else fileName++;
                            
                            wchar_t cleanName[256];
                            CleanCloudFileName(cleanName, fileName);
                            
                            BOOL dup = FALSE;
                            for (int k = 0; k < count; k++) {
                               if (_wcsicmp(temp_files[k].name, cleanName) == 0) {
                                    dup = TRUE; break;
                                }
                            }
                            
                            if (!dup && count < 500 && wcslen(cleanName) > 0) {
                                wcsncpy(temp_files[count].name, cleanName, 255);
                                temp_files[count].name[255] = L'\0';
                                
                                wchar_t* tPtr = wcsstr(valueData, L"T01D");
                                if (!tPtr) tPtr = wcsstr(valueData, L"t01d");
                                ULONGLONG ftVal = 0;
                                if (tPtr) {
                                    wchar_t hexStr[17];
                                    wcsncpy(hexStr, tPtr + 1, 16);
                                    hexStr[16] = L'\0';
                                    ftVal = _wcstoui64(hexStr, NULL, 16);
                                }
                                if (ftVal == 0) ftVal = 100 - j;
                                
                                temp_files[count].time.dwHighDateTime = (DWORD)(ftVal >> 32);
                                temp_files[count].time.dwLowDateTime = (DWORD)(ftVal & 0xFFFFFFFF);
                                count++;
                            }
                        }
                    }
                    RegCloseKey(hKeyServerMru);
                }
            }
        }
        RegCloseKey(hKeyRoot);
    }

    HKEY hKeyLegacy;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Office\\16.0\\Word\\File MRU", 0, KEY_READ, &hKeyLegacy) == ERROR_SUCCESS) {
        for (int j = 1; j <= 30; j++) {
            wchar_t valueName[32];
            wsprintfW(valueName, L"Item %d", j);
            
            wchar_t valueData[2048]; 
            DWORD dataSize = sizeof(valueData) - sizeof(wchar_t);
            ZeroMemory(valueData, sizeof(valueData));
            
            if (RegQueryValueExW(hKeyLegacy, valueName, NULL, NULL, (BYTE*)valueData, &dataSize) == ERROR_SUCCESS) {
                valueData[2047] = L'\0';
                wchar_t* pathStart = wcschr(valueData, L'*');
                if (pathStart) {
                    pathStart++;
                    wchar_t* fileName = wcsrchr(pathStart, L'\\');
                    if (!fileName) fileName = wcsrchr(pathStart, L'/');
                    if (!fileName) fileName = pathStart;
                    else fileName++;
                    
                    wchar_t cleanName[256];
                    CleanCloudFileName(cleanName, fileName);
                    
                    BOOL dup = FALSE;
                    for (int k = 0; k < count; k++) {
                        if (_wcsicmp(temp_files[k].name, cleanName) == 0) {
                            dup = TRUE; break;
                        }
                    }
                    
                    if (!dup && count < 500 && wcslen(cleanName) > 0) {
                        wcsncpy(temp_files[count].name, cleanName, 255);
                        temp_files[count].name[255] = L'\0';
                        
                        wchar_t* tPtr = wcsstr(valueData, L"T01D");
                        if (!tPtr) tPtr = wcsstr(valueData, L"t01d");
                        ULONGLONG ftVal = 0;
                        if (tPtr) {
                            wchar_t hexStr[17];
                            wcsncpy(hexStr, tPtr + 1, 16);
                            hexStr[16] = L'\0';
                            ftVal = _wcstoui64(hexStr, NULL, 16);
                        }
                        if (ftVal == 0) ftVal = 100 - j;
                        
                        temp_files[count].time.dwHighDateTime = (DWORD)(ftVal >> 32);
                        temp_files[count].time.dwLowDateTime = (DWORD)(ftVal & 0xFFFFFFFF);
                        count++;
                    }
                }
            }
        }
        RegCloseKey(hKeyLegacy);
    }

    if (count == 0) return;

    qsort(temp_files, count, sizeof(RecentFileInfo), CompareFileTimes);

    int max_to_take = count < 5 ? count : 5;
    for (int i = 0; i < max_to_take; i++) {
        wchar_t finalClean[256];
        wcsncpy(finalClean, temp_files[i].name, 255);
        finalClean[255] = L'\0';

        wchar_t* dot = wcsrchr(finalClean, L'.');
        if (dot && (_wcsicmp(dot, L".docx") == 0 || _wcsicmp(dot, L".doc") == 0)) {
            *dot = L'\0';
        }
        wcsncpy(recent_files[i], finalClean, 255);
        recent_files[i][255] = L'\0';
    }
}

void DrawPageContent(HDC hdc, int page, double shade) {
    COLORREF bgColor = RGB((int)(43 * shade), (int)(87 * shade), (int)(154 * shade)); 
    COLORREF textColor = RGB((int)(255 * shade), (int)(255 * shade), (int)(255 * shade));

    HBRUSH hBrush = CreateSolidBrush(bgColor);
    RECT rectTile = {0, 0, g_tileSize, g_tileSize};
    FillRect(hdc, &rectTile, hBrush);
    DeleteObject(hBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);

    if (page == 0) {
        HFONT hFontWord = CreateFontW((int)(28 * g_scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFontWord);
        DrawTextW(hdc, L"Word", -1, &rectTile, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        DeleteObject(hFontWord);
    } else {
        HFONT hFontTitle = CreateFontW((int)(13 * g_scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFontTitle);
        
        RECT rectHeader = { (int)(10 * g_scale), (int)(8 * g_scale), g_tileSize - (int)(10 * g_scale), (int)(24 * g_scale) };
        DrawTextW(hdc, L"RÉCENTS", -1, &rectHeader, DT_LEFT | DT_SINGLELINE);
        
        HFONT hFontFiles = CreateFontW((int)(11 * g_scale), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, 
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SelectObject(hdc, hFontFiles);

        for (int i = 0; i < 5; i++) {
            int paddingLeft = (int)(10 * g_scale);
            int paddingTop = (int)((28 + (i * 23)) * g_scale);
            int width = g_tileSize - paddingLeft;
            int height = paddingTop + (int)(20 * g_scale);
            
            RECT rectFile = { paddingLeft, paddingTop, width, height };
            // Correction appliquée ici : hdc remplace correctement hwnd 🎯
            DrawTextW(hdc, recent_files[i], -1, &rectFile, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        SelectObject(hdc, hOldFont);
        DeleteObject(hFontTitle);
        DeleteObject(hFontFiles);
    }
}

void ShowContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1003, L"Afficher la tuile");
    AppendMenuW(hMenu, MF_STRING, 1001, L"Masquer la tuile");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1002, L"Quitter");
    
    POINT pt;
    GetCursorPos(&pt);
    
    SetForegroundWindow(hwnd); 
    int id = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    
    if (id == 1001) {
        ShowWindow(hwnd, SW_HIDE); 
    } else if (id == 1003) {
        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else if (id == 1002) {
        DestroyWindow(hwnd); 
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            RefreshRecentWordFiles();
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetTimer(hwnd, TIMER_CHANGE_PAGE, 4000, NULL);

            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wcscpy(nid.szTip, L"Live Tile Word");
            Shell_NotifyIconW(NIM_ADD, &nid);
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowContextMenu(hwnd); 
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            ShellExecuteW(NULL, L"open", L"winword.exe", NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            is_dragging = 1;
            SetCapture(hwnd); 
            GetCursorPos(&drag_start);
            GetWindowRect(hwnd, &window_start);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (is_dragging) {
                POINT pt;
                GetCursorPos(&pt);
                int dx = pt.x - drag_start.x;
                int dy = pt.y - drag_start.y;
                SetWindowPos(hwnd, HWND_BOTTOM, window_start.left + dx, window_start.top + dy, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (is_dragging) {
                is_dragging = 0;
                ReleaseCapture();

                RECT rect;
                if (GetWindowRect(hwnd, &rect)) {
                    HKEY hKey;
                    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                        RegSetValueExW(hKey, L"TileX", 0, REG_DWORD, (BYTE*)&rect.left, sizeof(DWORD));
                        RegSetValueExW(hKey, L"TileY", 0, REG_DWORD, (BYTE*)&rect.top, sizeof(DWORD));
                        RegCloseKey(hKey);
                    }
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            ShowContextMenu(hwnd); 
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_CHANGE_PAGE) {
                is_animating = 1;
                anim_frame = 0;
                RefreshRecentWordFiles(); 
                KillTimer(hwnd, TIMER_CHANGE_PAGE);
                SetTimer(hwnd, TIMER_ANIMATION, 16, NULL);
            }
            
            if (wParam == TIMER_ANIMATION) {
                anim_frame++;
                if (anim_frame >= max_frames) {
                    KillTimer(hwnd, TIMER_ANIMATION);
                    current_page = !current_page;
                    is_animating = 0;
                    SetTimer(hwnd, TIMER_CHANGE_PAGE, 4000, NULL);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, g_tileSize, g_tileSize);
            HBITMAP hOldMem = (HBITMAP)SelectObject(hdcMem, hbmMem);

            HDC hdcPage = CreateCompatibleDC(hdc);
            HBITMAP hbmPage = CreateCompatibleBitmap(hdc, g_tileSize, g_tileSize);
            HBITMAP hOldPage = (HBITMAP)SelectObject(hdcPage, hbmPage);

            SetStretchBltMode(hdcMem, HALFTONE);

            if (!is_animating) {
                DrawPageContent(hdcMem, current_page, 1.0);
            } else {
                double progress = (double)anim_frame / (double)max_frames;
                double shade1 = cos(progress * (PI / 2.0));
                double shade2 = sin(progress * (PI / 2.0));

                int h1 = (int)(g_tileSize * (1.0 - progress));
                int h2 = g_tileSize - h1;
                int next_page = !current_page;

                if (h1 > 0) {
                    DrawPageContent(hdcPage, current_page, shade1);
                    StretchBlt(hdcMem, 0, 0, g_tileSize, h1, hdcPage, 0, 0, g_tileSize, g_tileSize, SRCCOPY);
                }
                if (h2 > 0) {
                    DrawPageContent(hdcPage, next_page, shade2);
                    StretchBlt(hdcMem, 0, h1, g_tileSize, h2, hdcPage, 0, 0, g_tileSize, g_tileSize, SRCCOPY);
                }
            }

            BitBlt(hdc, 0, 0, g_tileSize, g_tileSize, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcPage, hOldPage);
            DeleteObject(hbmPage);
            DeleteDC(hdcPage);
            SelectObject(hdcMem, hOldMem);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);

            EndPaint(hwnd, &ps);
            return 0;
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

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HDC hdcScreen = GetDC(NULL);
    int dpi = GetDeviceCaps(hdcScreen, LOGPIXELSX);
    ReleaseDC(NULL, hdcScreen);
    
    g_scale = (double)dpi / 96.0;         
    g_tileSize = (int)(150 * g_scale);   

    int posX = 200;
    int posY = 200;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwX = 0, dwY = 0;
        DWORD dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"TileX", NULL, NULL, (BYTE*)&dwX, &dwSize) == ERROR_SUCCESS &&
            RegQueryValueExW(hKey, L"TileY", NULL, NULL, (BYTE*)&dwY, &dwSize) == ERROR_SUCCESS) {
            posX = (int)dwX;
            posY = (int)dwY;
        }
        RegCloseKey(hKey);
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t CLASS_NAME[] = L"LiveTileWordDesktopClass";

    WNDCLASSW wc = {0};
    wc.style = CS_DBLCLKS; 
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, 
        CLASS_NAME, L"Tile Word Bureau",
        WS_POPUP | WS_VISIBLE,
        posX, posY, g_tileSize, g_tileSize, 
        NULL, NULL, hInstance, NULL
    );

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}