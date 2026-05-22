#define _WIN32_WINNT 0x0A00 

#include <windows.h>
#include <shellapi.h> 
#include <wininet.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>

#define TIMER_WEATHER_ANIM  1
#define TIMER_REFRESH_DATA  2
#define TILE_SIZE           150
#define PI                  3.14159265358979323846

#define WM_TRAYICON         (WM_USER + 1)
#define ID_TRAY_ICON        1
#define REG_KEY_PATH        L"Software\\LiveTileWeatherDesktop"

// États météo
#define WEATHER_CLEAR   0
#define WEATHER_RAIN    1
#define WEATHER_SNOW    2
#define WEATHER_CLOUDY  3

// Paramètres des particules pour l'animation
#define MAX_PARTICLES 35
typedef struct {
    double x, y;
    double speed;
    double size;
} Particle;

Particle g_particles[MAX_PARTICLES];
double g_animTime = 0.0;

// Variables globales de la météo
wchar_t g_location[64] = L"Détection...";
wchar_t g_temp[16] = L"--°C";
wchar_t g_condition[64] = L"Connexion...";
int g_weatherState = WEATHER_CLEAR;

// Fenêtre et traînée
int is_dragging = 0;
POINT drag_start;
RECT window_start;

// Initialise le générateur de particules selon le temps qu'il fait 🛠️
void InitParticles() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        g_particles[i].x = rand() % TILE_SIZE;
        g_particles[i].y = rand() % TILE_SIZE;
        if (g_weatherState == WEATHER_RAIN) {
            g_particles[i].speed = 5.0 + (rand() % 50) / 10.0;
            g_particles[i].size = 8.0 + (rand() % 6);
        } else if (g_weatherState == WEATHER_SNOW) {
            g_particles[i].speed = 0.8 + (rand() % 10) / 10.0;
            g_particles[i].size = 2.0 + (rand() % 3);
        } else { 
            g_particles[i].speed = 0.1 + (rand() % 15) / 100.0;
            g_particles[i].size = 30.0 + (rand() % 20);
        }
    }
}

// Thread en arrière-plan compatible 64 bits (LPVOID et handles préservés) 📡
DWORD WINAPI FetchWeatherThread(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;
    HINTERNET hInternet = InternetOpenW(L"LiveTileWeather64", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return 0;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, L"http://wttr.in/?format=%l|%t|%C", NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return 0; }

    char data[512] = {0};
    DWORD bytesRead = 0;
    InternetReadFile(hUrl, data, sizeof(data) - 1, &bytesRead);
    
    if (bytesRead > 0) {
        wchar_t wData[512] = {0};
        MultiByteToWideChar(CP_UTF8, 0, data, -1, wData, 512);

        wchar_t* token1 = wcschr(wData, L'|');
        if (token1) {
            *token1 = L'\0';
            wcsncpy(g_location, wData, 63);
            
            wchar_t* comma = wcschr(g_location, L',');
            if (comma) *comma = L'\0';

            wchar_t* token2 = wcschr(token1 + 1, L'|');
            if (token2) {
                *token2 = L'\0';
                wcsncpy(g_temp, token1 + 1, 15);
                wcsncpy(g_condition, token2 + 1, 63);

                wchar_t* newline = wcschr(g_condition, L'\n');
                if (newline) *newline = L'\0';
                newline = wcschr(g_condition, L'\r');
                if (newline) *newline = L'\0';

                _wcslwr(g_condition); 
                if (wcsstr(g_condition, L"rain") || wcsstr(g_condition, L"drizzle") || wcsstr(g_condition, L"pluie") || wcsstr(g_condition, L"averse")) {
                    g_weatherState = WEATHER_RAIN;
                } else if (wcsstr(g_condition, L"snow") || wcsstr(g_condition, L"neige") || wcsstr(g_condition, L"flurry")) {
                    g_weatherState = WEATHER_SNOW;
                } else if (wcsstr(g_condition, L"cloud") || wcsstr(g_condition, L"cloudy") || wcsstr(g_condition, L"nuage") || wcsstr(g_condition, L"couvert") || wcsstr(g_condition, L"overcast")) {
                    g_weatherState = WEATHER_CLOUDY;
                } else {
                    g_weatherState = WEATHER_CLEAR;
                }
                g_condition[0] = towupper(g_condition[0]);
                InitParticles();
            }
        }
    }

    InternetCloseHandle(hUrl); InternetCloseHandle(hInternet);
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
}

// Dessine le fond et met à jour les éléments animés (Soleil amélioré ici !) 🎨
void DrawWeatherContent(HDC hdc) {
    COLORREF topColor, bottomColor;

    if (g_weatherState == WEATHER_RAIN) {
        topColor = RGB(44, 62, 80); bottomColor = RGB(125, 135, 145);
    } else if (g_weatherState == WEATHER_SNOW) {
        topColor = RGB(116, 140, 160); bottomColor = RGB(220, 225, 230);
    } else if (g_weatherState == WEATHER_CLOUDY) {
        topColor = RGB(75, 100, 125); bottomColor = RGB(140, 155, 170);
    } else { 
        topColor = RGB(0, 130, 230); bottomColor = RGB(50, 180, 255);
    }

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

    // --- MOTEUR D'ANIMATION ---
    if (g_weatherState == WEATHER_RAIN) {
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(170, 210, 240));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        for (int i = 0; i < MAX_PARTICLES; i++) {
            g_particles[i].y += g_particles[i].speed;
            if (g_particles[i].y > TILE_SIZE) { g_particles[i].y = -10; g_particles[i].x = rand() % TILE_SIZE; }
            MoveToEx(hdc, (int)g_particles[i].x, (int)g_particles[i].y, NULL);
            LineTo(hdc, (int)(g_particles[i].x - 1), (int)(g_particles[i].y + g_particles[i].size));
        }
        SelectObject(hdc, hOldPen); DeleteObject(hPen);
    } 
    else if (g_weatherState == WEATHER_SNOW) {
        HBRUSH hSnowBrush = CreateSolidBrush(RGB(255, 255, 255));
        for (int i = 0; i < MAX_PARTICLES; i++) {
            g_particles[i].y += g_particles[i].speed;
            g_particles[i].x += sin(g_particles[i].y / 12.0) * 0.3; 
            if (g_particles[i].y > TILE_SIZE) { g_particles[i].y = -5; g_particles[i].x = rand() % TILE_SIZE; }
            RECT rSnow = {(int)g_particles[i].x, (int)g_particles[i].y, (int)(g_particles[i].x + g_particles[i].size), (int)(g_particles[i].y + g_particles[i].size)};
            FillRect(hdc, &rSnow, hSnowBrush);
        }
        DeleteObject(hSnowBrush);
    } 
    else if (g_weatherState == WEATHER_CLOUDY) {
        HBRUSH hCloudBrush = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH hOld = (HBRUSH)SelectObject(hdc, hCloudBrush);
        HPEN hNoPen = CreatePen(PS_NULL, 0, 0);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hNoPen);
        for (int i = 0; i < 8; i++) { 
            g_particles[i].x += g_particles[i].speed;
            if (g_particles[i].x > TILE_SIZE) { g_particles[i].x = -g_particles[i].size; g_particles[i].y = rand() % (TILE_SIZE - 40); }
            Ellipse(hdc, (int)g_particles[i].x, (int)g_particles[i].y, (int)(g_particles[i].x + g_particles[i].size), (int)(g_particles[i].y + g_particles[i].size * 0.6));
        }
        SelectObject(hdc, hOldPen); DeleteObject(hNoPen);
        SelectObject(hdc, hOld); DeleteObject(hCloudBrush);
    } 
    else if (g_weatherState == WEATHER_CLEAR) {
        // --- NOUVEAU SOLEIL AMÉLIORÉ ☀️ ---
        g_animTime += 0.03; 
        int cx = TILE_SIZE - 35; 
        int cy = 35;
        int baseRadius = 16;
        int pulse = (int)(sin(g_animTime * 2.5) * 2);

        // 1. Dessin de l'Aura/Couronne dorée (Effet lueur pulsante)
        HBRUSH hAuraBrush = CreateSolidBrush(RGB(255, 170, 40));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hAuraBrush);
        HPEN hNoPen = CreatePen(PS_NULL, 0, 0);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hNoPen);

        Ellipse(hdc, cx - baseRadius - 6 - pulse, cy - baseRadius - 6 - pulse, 
                cx + baseRadius + 6 + pulse, cy + baseRadius + 6 + pulse);

        // 2. Dessin des rayons rotatifs dynamiques
        HPEN hRayPen = CreatePen(PS_SOLID, 2, RGB(255, 210, 0));
        SelectObject(hdc, hRayPen);
        int numRays = 8;
        for (int r = 0; r < numRays; r++) {
            double angle = (r * (2.0 * PI / numRays)) + g_animTime;
            int startDist = baseRadius + 3;
            int endDist = baseRadius + 11 + pulse;
            int x1 = cx + (int)(cos(angle) * startDist);
            int y1 = cy + (int)(sin(angle) * startDist);
            int x2 = cx + (int)(cos(angle) * endDist);
            int y2 = cy + (int)(sin(angle) * endDist);
            MoveToEx(hdc, x1, y1, NULL);
            LineTo(hdc, x2, y2);
        }
        SelectObject(hdc, hOldPen); DeleteObject(hRayPen);

        // 3. Cœur du soleil (Brillant au centre)
        HBRUSH hSunBrush = CreateSolidBrush(RGB(255, 245, 120));
        SelectObject(hdc, hSunBrush);
        Ellipse(hdc, cx - baseRadius, cy - baseRadius, cx + baseRadius, cy + baseRadius);

        SelectObject(hdc, hOldBrush); DeleteObject(hSunBrush); 
        DeleteObject(hAuraBrush); DeleteObject(hNoPen);
    }

    // --- EN-TÊTE TEXTE ---
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    HFONT hFontLoc = CreateFontW(14, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontLoc);
    RECT rLoc = {10, 10, TILE_SIZE - 60, 30}; // Légèrement décalé pour ne pas chevaucher le soleil
    DrawTextW(hdc, g_location, -1, &rLoc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    HFONT hFontTemp = CreateFontW(36, 0, 0, 0, FW_LIGHT, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(hdc, hFontTemp);
    RECT rTemp = {10, 40, TILE_SIZE - 10, 90};
    DrawTextW(hdc, g_temp, -1, &rTemp, DT_LEFT | DT_SINGLELINE);

    HFONT hFontCond = CreateFontW(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(hdc, hFontCond);
    RECT rCond = {10, TILE_SIZE - 25, TILE_SIZE - 10, TILE_SIZE - 5};
    DrawTextW(hdc, g_condition, -1, &rCond, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFontLoc); DeleteObject(hFontTemp); DeleteObject(hFontCond);
}

void ShowContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1003, L"Afficher la tuile");
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
            srand((unsigned int)time(NULL));
            InitParticles();
            
            CreateThread(NULL, 0, FetchWeatherThread, hwnd, 0, NULL);
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            
            SetTimer(hwnd, TIMER_WEATHER_ANIM, 16, NULL);
            SetTimer(hwnd, TIMER_REFRESH_DATA, 15 * 60 * 1000, NULL);

            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wcscpy(nid.szTip, L"Live Tile Météo 64 bits");
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
            if (wParam == TIMER_WEATHER_ANIM) InvalidateRect(hwnd, NULL, FALSE);
            if (wParam == TIMER_REFRESH_DATA) CreateThread(NULL, 0, FetchWeatherThread, hwnd, 0, NULL);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, TILE_SIZE, TILE_SIZE);
            HBITMAP hOldMem = (HBITMAP)SelectObject(hdcMem, hbmMem);

            DrawWeatherContent(hdcMem);
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
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int main() {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t CLASS_NAME[] = L"LiveTileWeatherDesktop64Class";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    int pX = 200, pY = 200;
    HKEY hK; if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hK) == ERROR_SUCCESS) {
        DWORD x, y, s = 4;
        RegQueryValueExW(hK, L"TileX", 0, 0, (BYTE*)&x, &s);
        RegQueryValueExW(hK, L"TileY", 0, 0, (BYTE*)&y, &s);
        pX = x; pY = y; RegCloseKey(hK);
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, 
        CLASS_NAME, L"Tile Weather 64-bit",
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