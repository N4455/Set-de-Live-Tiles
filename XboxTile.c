#define _WIN32_WINNT 0x0A00 

#include <windows.h>
#include <shellapi.h> 
#include <wininet.h>
#include <olectl.h>
#include <ole2.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>
#include <stdio.h>

#define TIMER_SLIDER_ANIM   1
#define TIMER_REFRESH_XBOX  2
#define TIMER_NEXT_SLIDE    3

// Taille augmentée à 310 pour le format "Grande vignette" de Windows 📐
#define TILE_SIZE           310

#define WM_TRAYICON         (WM_USER + 1)
#define ID_TRAY_ICON        1
#define REG_KEY_PATH        L"Software\\LiveTileXBOXDesktop"

typedef struct {
    wchar_t title[128]; 
    IPicture* photo;     
    BOOL photoLoaded;
} XboxNewsItem;

#define MAX_POOL_SIZE 30
XboxNewsItem g_newsPool[MAX_POOL_SIZE];
int g_poolCount = 0;

int g_currentItemIdx = 0;
int g_nextItemIdx = 0;

double g_slideOffset = 0.0; 
int g_isTransitioning = 0;

int is_dragging = 0;
POINT drag_start;
RECT window_start;

// Nettoie proprement les balises CDATA et XML 🫧
void CleanRawXml(wchar_t* str) {
    wchar_t* cdata = wcsstr(str, L"<![CDATA[");
    if (cdata) {
        size_t len = wcslen(cdata + 9);
        memmove(str, cdata + 9, (len + 1) * sizeof(wchar_t));
    }
    wchar_t* cdataEnd = wcsstr(str, L"]]>");
    if (cdataEnd) {
        *cdataEnd = L'\0';
    }
}

// Décodeur d'entités HTML (ex: &#8211;) 🧼
void DecodeHtmlEntities(wchar_t* str) {
    wchar_t* p = str;
    while (*p) {
        if (*p == L'&') {
            if (*(p + 1) == L'#') {
                wchar_t* end;
                long val = wcstol(p + 2, &end, 10);
                if (*end == L';') {
                    *p = (wchar_t)val;
                    size_t remain = wcslen(end + 1);
                    memmove(p + 1, end + 1, (remain + 1) * sizeof(wchar_t));
                    continue;
                }
            } else if (wcsncmp(p, L"&amp;", 5) == 0) {
                *p = L'&';
                memmove(p + 1, p + 5, (wcslen(p + 5) + 1) * sizeof(wchar_t));
                continue;
            } else if (wcsncmp(p, L"&quot;", 6) == 0) {
                *p = L'"';
                memmove(p + 1, p + 6, (wcslen(p + 6) + 1) * sizeof(wchar_t));
                continue;
            }
        }
        p++;
    }
}

// Convertit un flux mémoire binaire brut d'image en objet GDI 🖼️
IPicture* LoadImageFromStream(const char* data, DWORD size) {
    if (!data || size == 0) return NULL;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hGlobal) return NULL;
    void* pData = GlobalLock(hGlobal);
    if (!pData) { GlobalFree(hGlobal); return NULL; }
    memcpy(pData, data, size);
    GlobalUnlock(hGlobal);
    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) != S_OK) {
        GlobalFree(hGlobal);
        return NULL;
    }
    IPicture* pPicture = NULL;
    if (OleLoadPicture(pStream, size, FALSE, &IID_IPicture, (void**)&pPicture) != S_OK) {
        pStream->lpVtbl->Release(pStream);
        return NULL;
    }
    pStream->lpVtbl->Release(pStream);
    return pPicture;
}

void ClearPool() {
    for (int i = 0; i < MAX_POOL_SIZE; i++) {
        if (g_newsPool[i].photo) g_newsPool[i].photo->lpVtbl->Release(g_newsPool[i].photo);
        g_newsPool[i].photo = NULL;
        g_newsPool[i].photoLoaded = FALSE;
    }
    g_poolCount = 0;
}

// Thread Réseau optimisé pour extraire les visuels WordPress 📡⚡
DWORD WINAPI FetchXboxNewsThread(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;
    
    HINTERNET hInternet = InternetOpenW(L"LiveTileXBOX64_PRO", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return 0;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, L"https://news.xbox.com/en-us/feed/", NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return 0;
    }

    ClearPool(); 

    DWORD xmlBufferSize = 1024 * 1024;
    char* xmlBuffer = (char*)malloc(xmlBufferSize);
    if (!xmlBuffer) { InternetCloseHandle(hUrl); InternetCloseHandle(hInternet); return 0; }
    memset(xmlBuffer, 0, xmlBufferSize);

    DWORD bytesRead = 0, totalBytesRead = 0;
    while (InternetReadFile(hUrl, xmlBuffer + totalBytesRead, xmlBufferSize - totalBytesRead - 1, &bytesRead) && bytesRead > 0) {
        totalBytesRead += bytesRead;
    }
    xmlBuffer[totalBytesRead] = '\0';
    InternetCloseHandle(hUrl);

    if (totalBytesRead > 0) {
        char* itemPtr = strstr(xmlBuffer, "<item>");
        while (itemPtr && g_poolCount < MAX_POOL_SIZE) {
            char* itemEnd = strstr(itemPtr, "</item>");
            if (!itemEnd) break;
            
            char oldEndChar = *itemEnd;
            *itemEnd = '\0';
            
            char* titleStart = strstr(itemPtr, "<title>");
            if (titleStart) {
                titleStart += 7;
                char* titleEnd = strstr(titleStart, "</title>");
                if (titleEnd) {
                    char oldTitleChar = *titleEnd;
                    *titleEnd = '\0';
                    MultiByteToWideChar(CP_UTF8, 0, titleStart, -1, g_newsPool[g_poolCount].title, 127);
                    *titleEnd = oldTitleChar;
                    CleanRawXml(g_newsPool[g_poolCount].title);
                    DecodeHtmlEntities(g_newsPool[g_poolCount].title);
                }
            }
            
            char targetUrl[512] = {0};
            char* p = NULL;
            
            if ((p = strstr(itemPtr, "<media:content url=\""))) {
                p += 20; char* e = strchr(p, '"'); if (e) snprintf(targetUrl, sizeof(targetUrl), "%.*s", (int)(e - p), p);
            }
            if (strlen(targetUrl) == 0 && (p = strstr(itemPtr, "<media:thumbnail url=\""))) {
                p += 22; char* e = strchr(p, '"'); if (e) snprintf(targetUrl, sizeof(targetUrl), "%.*s", (int)(e - p), p);
            }
            if (strlen(targetUrl) == 0 && (p = strstr(itemPtr, "<enclosure url=\""))) {
                p += 16; char* e = strchr(p, '"'); if (e) snprintf(targetUrl, sizeof(targetUrl), "%.*s", (int)(e - p), p);
            }
            if (strlen(targetUrl) == 0 && (p = strstr(itemPtr, "src=\""))) {
                p += 5; char* e = strchr(p, '"'); if (e) snprintf(targetUrl, sizeof(targetUrl), "%.*s", (int)(e - p), p);
            }
            
            if (strlen(targetUrl) == 0) {
                char* searchPtr = itemPtr;
                while ((searchPtr = strstr(searchPtr, "https://"))) {
                    char* endOfUrl = searchPtr;
                    while (*endOfUrl && *endOfUrl != '"' && *endOfUrl != '\'' && *endOfUrl != ' ' && *endOfUrl != '<' && *endOfUrl != '>' && *endOfUrl != '&') {
                        endOfUrl++;
                    }
                    int len = (int)(endOfUrl - searchPtr);
                    if (len > 10 && len < 512) {
                        char testUrl[512];
                        snprintf(testUrl, sizeof(testUrl), "%.*s", len, searchPtr);
                        if (strstr(testUrl, ".jpg") || strstr(testUrl, ".jpeg") || strstr(testUrl, ".png") || strstr(testUrl, ".webp")) {
                            strcpy(targetUrl, testUrl);
                            break;
                        }
                    }
                    searchPtr++;
                }
            }
            
            if (strlen(targetUrl) > 0) {
                wchar_t wImgUrl[512] = {0};
                MultiByteToWideChar(CP_UTF8, 0, targetUrl, -1, wImgUrl, 511);
                
                HINTERNET hImgUrl = InternetOpenUrlW(hInternet, wImgUrl, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);
                if (hImgUrl) {
                    DWORD maxImgSize = 1024 * 1024; 
                    char* imgBuf = (char*)malloc(maxImgSize);
                    if (imgBuf) {
                        DWORD imgBytesRead = 0, totalImgBytes = 0;
                        while (InternetReadFile(hImgUrl, imgBuf + totalImgBytes, maxImgSize - totalImgBytes - 1, &imgBytesRead) && imgBytesRead > 0) {
                            totalImgBytes += imgBytesRead;
                            if (totalImgBytes >= maxImgSize - 1) break; 
                        }
                        if (totalImgBytes > 0) {
                            g_newsPool[g_poolCount].photo = LoadImageFromStream(imgBuf, totalImgBytes);
                            if (g_newsPool[g_poolCount].photo) g_newsPool[g_poolCount].photoLoaded = TRUE;
                        }
                        free(imgBuf);
                    }
                    InternetCloseHandle(hImgUrl);
                }
            }
            
            *itemEnd = oldEndChar;
            g_poolCount++;
            itemPtr = strstr(itemEnd + 7, "<item>");
        }
    }

    free(xmlBuffer);
    InternetCloseHandle(hInternet);
    
    if (g_poolCount > 0) {
        g_currentItemIdx = 0;
        SetTimer(hwnd, TIMER_NEXT_SLIDE, 4000, NULL);
    }

    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
}

// Dessin de la carte d'actu glissante au format Grande Vignette 🎨
void DrawNewsCard(HDC hdc, int poolIdx, int offsetX) {
    if (g_poolCount == 0 || poolIdx < 0 || poolIdx >= g_poolCount) return;

    HBRUSH hBgCard = CreateSolidBrush(RGB(10, 15, 10));
    RECT rBg = {offsetX, 40, offsetX + TILE_SIZE, TILE_SIZE};
    FillRect(hdc, &rBg, hBgCard);
    DeleteObject(hBgCard);

    // Ajustement de la zone d'affichage de la photo pour le grand format (60px réservés en bas pour le titre)
    if (g_newsPool[poolIdx].photoLoaded && g_newsPool[poolIdx].photo) {
        long width, height;
        g_newsPool[poolIdx].photo->lpVtbl->get_Width(g_newsPool[poolIdx].photo, &width);
        g_newsPool[poolIdx].photo->lpVtbl->get_Height(g_newsPool[poolIdx].photo, &height);
        
        g_newsPool[poolIdx].photo->lpVtbl->Render(
            g_newsPool[poolIdx].photo, hdc, 
            offsetX, 40, TILE_SIZE, TILE_SIZE - 100, 
            0, height, width, -height, NULL
        );
    } else {
        HBRUSH hIconBrush = CreateSolidBrush(RGB(16, 124, 16));
        RECT rIcon = {offsetX, 40, offsetX + TILE_SIZE, TILE_SIZE - 60};
        FillRect(hdc, &rIcon, hIconBrush);
        DeleteObject(hIconBrush);
    }

    // Bandeau noir textuel du bas étendu à 60px de hauteur 🖤
    HBRUSH hTextBg = CreateSolidBrush(RGB(10, 10, 10));
    RECT rTextBg = {offsetX, TILE_SIZE - 60, offsetX + TILE_SIZE, TILE_SIZE};
    FillRect(hdc, &rTextBg, hTextBg);
    DeleteObject(hTextBg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    // Police du titre légèrement agrandie (taille 13) pour s'adapter à la surface ✍️
    HFONT hFontTitle = CreateFontW(13, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontTitle);
    RECT rTitle = {offsetX + 12, TILE_SIZE - 52, offsetX + TILE_SIZE - 12, TILE_SIZE - 8};
    DrawTextW(hdc, g_newsPool[poolIdx].title, -1, &rTitle, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFontTitle);
}

void DrawXboxTileContent(HDC hdc) {
    COLORREF topColor = RGB(16, 124, 16);    
    COLORREF bottomColor = RGB(12, 70, 12); 

    for (int y = 0; y < TILE_SIZE; y++) {
        double ratio = (double)y / TILE_SIZE;
        int r = (int)(GetRValue(topColor) * (1.0 - ratio) + GetRValue(bottomColor) * ratio); 
        int g = (int)(GetGValue(topColor) * (1.0 - ratio) + GetGValue(bottomColor) * ratio);
        int b = (int)(GetBValue(topColor) * (1.0 - ratio) + GetBValue(bottomColor) * ratio);
        HBRUSH hLineBrush = CreateSolidBrush(RGB(r, g, b));
        RECT rLine = {0, y, TILE_SIZE, y + 1};
        FillRect(hdc, &rLine, hLineBrush);
        DeleteObject(hLineBrush);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    
    // Titre de l'en-tête adapté (taille 14) 🟢
    HFONT hFontHeader = CreateFontW(14, 0, 0, 0, FW_EXTRABOLD, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT hOld = SelectObject(hdc, hFontHeader);
    RECT rHeader = {15, 12, TILE_SIZE - 15, 38};
    DrawTextW(hdc, L"XBOX Wire Actu", -1, &rHeader, DT_LEFT | DT_SINGLELINE);
    SelectObject(hdc, hOld); DeleteObject(hFontHeader);

    if (g_poolCount > 0) {
        if (g_isTransitioning) {
            DrawNewsCard(hdc, g_currentItemIdx, (int)-g_slideOffset);
            DrawNewsCard(hdc, g_nextItemIdx, (int)(TILE_SIZE - g_slideOffset));
        } else {
            DrawNewsCard(hdc, g_currentItemIdx, 0);
        }
    }
}

void ShowContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1003, L"Afficher la tuile XBOX");
    AppendMenuW(hMenu, MF_STRING, 1001, L"Masquer la tuile");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1002, L"Quitter");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd); 
    int id = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
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
            OleInitialize(NULL); 
            CreateThread(NULL, 0, FetchXboxNewsThread, hwnd, 0, NULL);
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            
            SetTimer(hwnd, TIMER_SLIDER_ANIM, 16, NULL);
            SetTimer(hwnd, TIMER_REFRESH_XBOX, 30 * 60 * 1000, NULL); 

            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd; nid.uID = ID_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wcscpy(nid.szTip, L"XBOX Wire Live Tile Actu");
            Shell_NotifyIconW(NIM_ADD, &nid);
            return 0;
        }

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) ShowContextMenu(hwnd); 
            return 0;

        case WM_LBUTTONDOWN:
            is_dragging = 1; SetCapture(hwnd); GetCursorPos(&drag_start); GetWindowRect(hwnd, &window_start);
            return 0;
            
        case WM_MOUSEMOVE:
            if (is_dragging) {
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(hwnd, HWND_BOTTOM, window_start.left + (pt.x - drag_start.x), window_start.top + (pt.y - drag_start.y), 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
            
        case WM_LBUTTONUP:
            if (is_dragging) {
                is_dragging = 0; ReleaseCapture();
                RECT r; GetWindowRect(hwnd, &r);
                HKEY h; if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, 0, 0, KEY_WRITE, 0, &h, 0) == ERROR_SUCCESS) {
                    RegSetValueExW(h, L"TileX", 0, REG_DWORD, (BYTE*)&r.left, 4);
                    RegSetValueExW(h, L"TileY", 0, REG_DWORD, (BYTE*)&r.top, 4);
                    RegCloseKey(h);
                }
            }
            return 0;

        case WM_RBUTTONUP: ShowContextMenu(hwnd); return 0;

        case WM_TIMER:
            if (wParam == TIMER_SLIDER_ANIM) {
                if (g_isTransitioning) {
                    double remaining = TILE_SIZE - g_slideOffset;
                    if (remaining > 0.5) {
                        g_slideOffset += remaining * 0.12; 
                    } else {
                        g_isTransitioning = 0;
                        g_currentItemIdx = g_nextItemIdx;
                        g_slideOffset = 0.0;
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            
            if (wParam == TIMER_NEXT_SLIDE) {
                if (!g_isTransitioning && g_poolCount > 1) {
                    g_nextItemIdx = (g_currentItemIdx + 1) % g_poolCount;
                    g_slideOffset = 0.0;
                    g_isTransitioning = 1; 
                }
            }

            if (wParam == TIMER_REFRESH_XBOX) {
                CreateThread(NULL, 0, FetchXboxNewsThread, hwnd, 0, NULL);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, TILE_SIZE, TILE_SIZE);
            HBITMAP hOldMem = (HBITMAP)SelectObject(hdcMem, hbmMem);

            DrawXboxTileContent(hdcMem);
            BitBlt(hdc, 0, 0, TILE_SIZE, TILE_SIZE, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hOldMem); DeleteObject(hbmMem); DeleteDC(hdcMem);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            wp->hwndInsertAfter = HWND_BOTTOM; 
            wp->flags &= ~SWP_NOZORDER;
            return 0;
        }

        case WM_MOUSEACTIVATE: return MA_NOACTIVATE; 

        case WM_DESTROY: {
            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd; nid.uID = ID_TRAY_ICON;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            ClearPool(); 
            OleUninitialize();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int main() {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t CLASS_NAME[] = L"LiveTileXBOXDesktop64Class";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    int pX = 250, pY = 250;
    HKEY hK; if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hK) == ERROR_SUCCESS) {
        DWORD x, y, s = 4;
        RegQueryValueExW(hK, L"TileX", 0, 0, (BYTE*)&x, &s);
        RegQueryValueExW(hK, L"TileY", 0, 0, (BYTE*)&y, &s);
        pX = x; pY = y; RegCloseKey(hK);
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, 
        CLASS_NAME, L"XBOX Wire Actu 64-bit",
        WS_POPUP | WS_VISIBLE,
        pX, pY, TILE_SIZE, TILE_SIZE, 
        NULL, NULL, hInstance, NULL
    );

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}