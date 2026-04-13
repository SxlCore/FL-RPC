#include <windows.h>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <shlobj.h>
#include <ctime>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// original version.dll from C:/Windows imports
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

const std::string APP_ID = "1493357686362738831";
HANDLE hDiscordPipe = INVALID_HANDLE_VALUE;
HWND hOverlay = NULL;
std::string currentStatus = "Initializing...";

bool isMinimized = false;
bool isRpcEnabled = true;
bool g_ForceRpcUpdate = false;

HINSTANCE g_hInstance = NULL;

struct IPCHeader { int opcode; int length; };

std::map<std::string, std::string> g_flpPaths;
std::mutex g_mapMutex;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string res(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &res[0], size, NULL, NULL);
    return res;
}

// Logs
void Log(const std::string& msg) {
    static std::string logPath = "";
    if (logPath.empty()) {
        char path[MAX_PATH]; GetModuleFileNameA(g_hInstance, path, MAX_PATH);
        std::string fullPath(path); size_t pos = fullPath.find_last_of("\\/");
        if (pos != std::string::npos) logPath = fullPath.substr(0, pos) + "\\rpc_log.txt";
    }

    FILE* fp = _fsopen(logPath.c_str(), "a", _SH_DENYNO);
    if (!fp) {
        char docPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
            logPath = std::string(docPath) + "\\Image-Line\\rpc_log.txt";
            fp = _fsopen(logPath.c_str(), "a", _SH_DENYNO);
        }
    }
    if (fp) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(fp, "[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, msg.c_str());
        fclose(fp);
    }
}

// CreateFileW + CreateFileA hooks
typedef HANDLE(WINAPI* PFN_CREATEFILEW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* PFN_CREATEFILEA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
PFN_CREATEFILEW OriginalCreateFileW = nullptr;
PFN_CREATEFILEA OriginalCreateFileA = nullptr;

HANDLE WINAPI HookedCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName) {
        std::wstring path(lpFileName);
        if (path.length() > 4) {
            std::wstring ext = path.substr(path.length() - 4); std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".flp") {
                std::string u8Path = WStringToString(path);
                size_t slashPos = u8Path.find_last_of("/\\");
                std::string fileName = (slashPos == std::string::npos) ? u8Path : u8Path.substr(slashPos + 1);
                if (fileName.length() >= 4) fileName = fileName.substr(0, fileName.length() - 4);
                std::lock_guard<std::mutex> lock(g_mapMutex); g_flpPaths[ToLower(fileName)] = u8Path;
            }
        }
    }
    return OriginalCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

HANDLE WINAPI HookedCreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName) {
        std::string path(lpFileName);
        if (path.length() > 4) {
            std::string ext = path.substr(path.length() - 4); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".flp") {
                size_t slashPos = path.find_last_of("/\\");
                std::string fileName = (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
                if (fileName.length() >= 4) fileName = fileName.substr(0, fileName.length() - 4);
                std::lock_guard<std::mutex> lock(g_mapMutex); g_flpPaths[ToLower(fileName)] = path;
            }
        }
    }
    return OriginalCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

void DetourIAT(void* moduleBase, const char* dllName, const char* funcName, void* newFunc) {
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)moduleBase;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)moduleBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return;

    IMAGE_DATA_DIRECTORY importsDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importsDirectory.VirtualAddress == 0) return;

    PIMAGE_IMPORT_DESCRIPTOR importDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)moduleBase + importsDirectory.VirtualAddress);
    while (importDescriptor->Name) {
        if (_stricmp((const char*)((BYTE*)moduleBase + importDescriptor->Name), dllName) == 0) {
            PIMAGE_THUNK_DATA originalFirstThunk = (PIMAGE_THUNK_DATA)((BYTE*)moduleBase + importDescriptor->OriginalFirstThunk);
            PIMAGE_THUNK_DATA firstThunk = (PIMAGE_THUNK_DATA)((BYTE*)moduleBase + importDescriptor->FirstThunk);

            while (originalFirstThunk->u1.AddressOfData) {
                if (!(originalFirstThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)((BYTE*)moduleBase + originalFirstThunk->u1.AddressOfData);
                    if (_stricmp(importByName->Name, funcName) == 0) {
                        DWORD oldProtect; VirtualProtect(&firstThunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
#ifdef _WIN64
                        firstThunk->u1.Function = (ULONGLONG)newFunc;
#else
                        firstThunk->u1.Function = (DWORD)newFunc;
#endif
                        VirtualProtect(&firstThunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                        return;
                    }
                }
                originalFirstThunk++; firstThunk++;
            }
        }
        importDescriptor++;
    }
}

std::string SearchRegistryForFile(HKEY hKey, const std::string& targetProject, int depth = 0) {
    if (depth > 2) return "";
    char keyName[256]; char valueName[256]; char data[1024];
    DWORD nameSize, dataSize, type;

    DWORD vIndex = 0;
    while (true) {
        nameSize = sizeof(valueName); dataSize = sizeof(data);
        if (RegEnumValueA(hKey, vIndex, valueName, &nameSize, NULL, &type, (LPBYTE)data, &dataSize) != ERROR_SUCCESS) break;
        if (type == REG_SZ) {
            std::string val(data);
            std::string lowerVal = ToLower(val);
            std::string lowerTarget = ToLower(targetProject);
            if (lowerVal.find(".flp") != std::string::npos && lowerVal.find(lowerTarget) != std::string::npos) return val;
        }
        vIndex++;
    }

    DWORD kIndex = 0;
    while (true) {
        nameSize = sizeof(keyName);
        if (RegEnumKeyExA(hKey, kIndex, keyName, &nameSize, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        HKEY hSubKey;
        if (RegOpenKeyExA(hKey, keyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
            std::string res = SearchRegistryForFile(hSubKey, targetProject, depth + 1);
            RegCloseKey(hSubKey);
            if (!res.empty()) return res;
        }
        kIndex++;
    }
    return "";
}

std::string GetPathFromRegistry(const std::string& projectName) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Image-Line", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        std::string path = SearchRegistryForFile(hKey, projectName + ".flp");
        RegCloseKey(hKey);
        return path;
    }
    return "";
}

void CheckCommandLineForFLP() {
    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            std::wstring arg = argv[i];
            if (arg.length() > 4) {
                std::wstring ext = arg.substr(arg.length() - 4); std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext == L".flp") {
                    std::string u8Path = WStringToString(arg);
                    size_t slashPos = u8Path.find_last_of("/\\");
                    std::string fileName = (slashPos == std::string::npos) ? u8Path : u8Path.substr(slashPos + 1);
                    if (fileName.length() >= 4) fileName = fileName.substr(0, fileName.length() - 4);
                    std::lock_guard<std::mutex> lock(g_mapMutex); g_flpPaths[ToLower(fileName)] = u8Path;
                }
            }
        }
        LocalFree(argv);
    }
}

std::string GetExactProjectPath(const std::string& projectName) {
    std::lock_guard<std::mutex> lock(g_mapMutex);
    auto it = g_flpPaths.find(ToLower(projectName));
    if (it != g_flpPaths.end()) return it->second;

    std::string regPath = GetPathFromRegistry(projectName);
    if (!regPath.empty()) {
        g_flpPaths[ToLower(projectName)] = regPath;
        return regPath;
    }
    return "";
}

bool ParseFlpTime(const std::string& filepath, int& hours, int& minutes) {
    hours = 0; minutes = 0;
    if (filepath.empty()) return false;

    FILE* fp = _fsopen(filepath.c_str(), "rb", _SH_DENYNO);
    if (!fp) return false;

    fseek(fp, 0, SEEK_END); size_t size = ftell(fp); fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> buf(size); fread(buf.data(), 1, size, fp); fclose(fp);

    if (size < 14 || memcmp(&buf[0], "FLhd", 4) != 0) return true;
    size_t pos = 4; uint32_t headerLen = *(uint32_t*)&buf[pos]; pos += 4 + headerLen;
    if (pos + 8 > size || memcmp(&buf[pos], "FLdt", 4) != 0) return true;
    pos += 8;

    while (pos < size) {
        uint8_t eventID = buf[pos++]; uint32_t len = 0;
        if (eventID < 64) len = 1; else if (eventID < 128) len = 2; else if (eventID < 192) len = 4; else {
            int shift = 0; while (pos < size) { uint8_t b = buf[pos++]; len |= (b & 0x7F) << shift; if ((b & 0x80) == 0) break; shift += 7; }
        }
        if (pos + len > size) break;

        if (eventID == 237 && len >= 8) {
            double best_time = 0.0;
            bool found = false;
            for (uint32_t i = 0; i + 8 <= len; i += 8) {
                double val = 0;
                memcpy(&val, &buf[pos + i], 8);
                if (val >= 0.0 && val < 3650.0) {
                    best_time = val; found = true;
                }
            }
            if (found) {
                hours = static_cast<int>(best_time * 24.0);
                minutes = static_cast<int>((best_time * 24.0 - hours) * 60.0);
                Log("Parsed Time: " + std::to_string(hours) + "h " + std::to_string(minutes) + "m");
            }
            return true;
        }
        pos += len;
    }
    return true;
}

// Discord rpc
void SetStatus(const std::string& status) {
    if (currentStatus != status) {
        currentStatus = status; Log("Status: " + status);
        if (hOverlay) InvalidateRect(hOverlay, NULL, TRUE);
    }
}

bool ReadDiscordResponse() {
    IPCHeader resHeader; DWORD bytesRead;
    if (ReadFile(hDiscordPipe, &resHeader, sizeof(resHeader), &bytesRead, NULL) && bytesRead == sizeof(resHeader)) {
        std::vector<char> buffer(resHeader.length + 1, 0); ReadFile(hDiscordPipe, buffer.data(), resHeader.length, &bytesRead, NULL);
        return true;
    }
    return false;
}

void ConnectDiscord() {
    if (!isRpcEnabled) return;
    if (hDiscordPipe != INVALID_HANDLE_VALUE) { CloseHandle(hDiscordPipe); hDiscordPipe = INVALID_HANDLE_VALUE; }
    SetStatus("Connecting...");
    hDiscordPipe = CreateFileA("\\\\.\\pipe\\discord-ipc-0", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDiscordPipe == INVALID_HANDLE_VALUE) { SetStatus("Discord: Not Found"); return; }

    std::string handshake = "{\"v\": 1, \"client_id\": \"" + APP_ID + "\"}";
    IPCHeader header = { 0, (int)handshake.length() }; DWORD bytesWritten;
    WriteFile(hDiscordPipe, &header, sizeof(header), &bytesWritten, NULL);
    WriteFile(hDiscordPipe, handshake.c_str(), handshake.length(), &bytesWritten, NULL);

    if (ReadDiscordResponse()) SetStatus("Discord: Connected");
    else { SetStatus("Discord: Handshake Failed"); CloseHandle(hDiscordPipe); hDiscordPipe = INVALID_HANDLE_VALUE; }
}

void SetDiscordActivity(const std::string& projectName, int hours, int minutes, long long sessionStartUnix) {
    if (hDiscordPipe == INVALID_HANDLE_VALUE || !isRpcEnabled) return;
    char payload[2048];
    std::string fileStr = (projectName == "New Project") ? "" : ".flp";

    sprintf_s(payload, sizeof(payload),
        "{\"cmd\": \"SET_ACTIVITY\", \"args\": {\"pid\": %d, \"activity\": {"
        "\"details\": \"Working on %s%s\", "
        "\"state\": \"Total project time - %dh %dm\", "
        "\"timestamps\": {\"start\": %lld}, "
        "\"assets\": {\"large_image\": \"flstudio\", \"large_text\": \"FL Studio\"}, "
        "\"buttons\": [{\"label\": \"Download on GitHub\", \"url\": \"https://github.com/sxlcore/FL-RPC\"}]"
        "}}, \"nonce\": \"1\"}", GetCurrentProcessId(), projectName.c_str(), fileStr.c_str(), hours, minutes, sessionStartUnix);

    std::string payloadStr = payload; IPCHeader header = { 1, (int)payloadStr.length() }; DWORD bytesWritten;
    if (!WriteFile(hDiscordPipe, &header, sizeof(header), &bytesWritten, NULL) || !WriteFile(hDiscordPipe, payloadStr.c_str(), payloadStr.length(), &bytesWritten, NULL)) {
        SetStatus("Discord: Disconnected"); CloseHandle(hDiscordPipe); hDiscordPipe = INVALID_HANDLE_VALUE; return;
    }
    ReadDiscordResponse();
}

// Overlay (in fl studio)
LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool isDotHovered = false;
    static bool isEyeHovered = false;
    static TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH bgTransparent = CreateSolidBrush(RGB(1, 1, 1)); FillRect(hdc, &ps.rcPaint, bgTransparent); DeleteObject(bgTransparent);

        if (isMinimized) {
            HBRUSH dotBrush = CreateSolidBrush(isDotHovered ? RGB(255, 100, 100) : RGB(200, 50, 50));
            SelectObject(hdc, dotBrush); SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, 0, 0, 16, 16); DeleteObject(dotBrush);
        }
        else {
            HBRUSH boxBrush = CreateSolidBrush(RGB(35, 35, 35)); HPEN boxPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
            SelectObject(hdc, boxBrush); SelectObject(hdc, boxPen); RoundRect(hdc, 0, 0, 220, 30, 10, 10);

            HBRUSH dotBrush = CreateSolidBrush(isDotHovered ? RGB(100, 255, 100) : RGB(50, 200, 50));
            SelectObject(hdc, dotBrush); SelectObject(hdc, GetStockObject(NULL_PEN)); Ellipse(hdc, 195, 7, 211, 23); DeleteObject(dotBrush);

            HBRUSH eyeBgBrush = CreateSolidBrush(isEyeHovered ? RGB(80, 80, 80) : RGB(45, 45, 45));
            SelectObject(hdc, eyeBgBrush); SelectObject(hdc, GetStockObject(NULL_PEN));
            RoundRect(hdc, 170, 7, 186, 23, 4, 4);

            if (isRpcEnabled) {
                HPEN penWhite = CreatePen(PS_SOLID, 2, RGB(220, 220, 220)); SelectObject(hdc, penWhite); SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, 172, 11, 184, 19);
                HBRUSH pupil = CreateSolidBrush(RGB(220, 220, 220)); SelectObject(hdc, pupil);
                Ellipse(hdc, 176, 13, 180, 17); DeleteObject(pupil); DeleteObject(penWhite);
            }
            else {
                HPEN penDark = CreatePen(PS_SOLID, 2, RGB(100, 100, 100)); SelectObject(hdc, penDark); SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, 172, 11, 184, 19);
                MoveToEx(hdc, 171, 9, NULL); LineTo(hdc, 185, 21); DeleteObject(penDark);
            }
            DeleteObject(eyeBgBrush);

            SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(220, 220, 220));
            HFONT hFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            SelectObject(hdc, hFont); RECT textRect = { 10, 0, 160, 30 }; DrawTextA(hdc, currentStatus.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            DeleteObject(hFont); DeleteObject(boxBrush); DeleteObject(boxPen);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    else if (msg == WM_LBUTTONDOWN) {
        int x = LOWORD(lParam);
        if (isMinimized) {
            isMinimized = false; InvalidateRect(hwnd, NULL, TRUE);
        }
        else {
            if (x >= 195 && x <= 211) {
                isMinimized = true; InvalidateRect(hwnd, NULL, TRUE);
            }
            else if (x >= 170 && x <= 186) {
                isRpcEnabled = !isRpcEnabled;
                if (!isRpcEnabled) {
                    if (hDiscordPipe != INVALID_HANDLE_VALUE) { CloseHandle(hDiscordPipe); hDiscordPipe = INVALID_HANDLE_VALUE; }
                    SetStatus("Discord: Disabled");
                }
                else {
                    SetStatus("Connecting...");
                    g_ForceRpcUpdate = true;
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        return 0;
    }
    else if (msg == WM_MOUSEMOVE) {
        int x = LOWORD(lParam);
        bool dHover = isMinimized ? true : (x >= 195 && x <= 211);
        bool eHover = !isMinimized && (x >= 170 && x <= 186);
        if (dHover != isDotHovered || eHover != isEyeHovered) { isDotHovered = dHover; isEyeHovered = eHover; InvalidateRect(hwnd, NULL, TRUE); }
        TrackMouseEvent(&tme); return 0;
    }
    else if (msg == WM_MOUSELEAVE) { isDotHovered = false; isEyeHovered = false; InvalidateRect(hwnd, NULL, TRUE); return 0; }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Main thread
void MainThread() {
    Log("DLL Started. Release Build with Buttons...");

    CheckCommandLineForFLP();
    OriginalCreateFileW = (PFN_CREATEFILEW)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileW");
    OriginalCreateFileA = (PFN_CREATEFILEA)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileA");

    DetourIAT(GetModuleHandleA(NULL), "kernel32.dll", "CreateFileW", (void*)HookedCreateFileW);
    DetourIAT(GetModuleHandleA(NULL), "kernel32.dll", "CreateFileA", (void*)HookedCreateFileA);

    HMODULE hEngine = NULL;
    while ((hEngine = GetModuleHandleA("FLEngine_x64.dll")) == NULL) Sleep(50);
    DetourIAT(hEngine, "kernel32.dll", "CreateFileW", (void*)HookedCreateFileW);
    DetourIAT(hEngine, "kernel32.dll", "CreateFileA", (void*)HookedCreateFileA);

    WNDCLASSA wc = { 0 }; wc.lpfnWndProc = OverlayProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = "FLRPCOverlayClass";
    RegisterClassA(&wc);
    hOverlay = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "FLRPCOverlayClass", "RPC Overlay", WS_POPUP, 0, 0, 220, 30, NULL, NULL, wc.hInstance, NULL);
    SetLayeredWindowAttributes(hOverlay, RGB(1, 1, 1), 0, LWA_COLORKEY);

    std::string currentProjectName = "";
    std::string currentProjectPath = "";
    int parsedHours = 0;
    int parsedMinutes = 0;
    long long sessionStartUnix = std::time(nullptr);
    bool forceRpcUpdate = false;
    ULONGLONG lastDiscordUpdateTick = 0;

    while (true) {
        MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }

        HWND hFlWindow = FindWindowA("TFruityLoopsMainForm", NULL);
        bool isForeground = (GetForegroundWindow() == hFlWindow || GetForegroundWindow() == hOverlay);

        if (hFlWindow && isForeground) {
            RECT rect; GetWindowRect(hFlWindow, &rect);
            int overlayW = isMinimized ? 16 : 220; int overlayH = isMinimized ? 16 : 30;
            SetWindowPos(hOverlay, HWND_TOPMOST, rect.right - overlayW - 25, rect.bottom - overlayH - 25, overlayW, overlayH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

            char windowTitle[256]; GetWindowTextA(hFlWindow, windowTitle, sizeof(windowTitle));
            std::string title(windowTitle);

            size_t flPos = title.find("- FL Studio");
            std::string projectName = "New Project";

            if (flPos != std::string::npos) {
                projectName = title.substr(0, flPos);
                while (!projectName.empty() && (projectName.back() == ' ' || projectName.back() == '*' || projectName.back() == '-')) projectName.pop_back();
                if (projectName.length() >= 4 && ToLower(projectName.substr(projectName.length() - 4)) == ".flp") {
                    projectName = projectName.substr(0, projectName.length() - 4);
                }
            }

            if (projectName != currentProjectName) {
                currentProjectName = projectName;
                currentProjectPath = "";
                parsedHours = 0;
                parsedMinutes = 0;
                sessionStartUnix = std::time(nullptr);
                forceRpcUpdate = true;
                Log("Detected project switch to: " + projectName);
            }

            if (currentProjectPath.empty() && projectName != "New Project") {
                std::string path = GetExactProjectPath(projectName);
                if (!path.empty()) {
                    if (ParseFlpTime(path, parsedHours, parsedMinutes)) {
                        currentProjectPath = path;
                        forceRpcUpdate = true;
                    }
                }
            }

            if (g_ForceRpcUpdate) {
                forceRpcUpdate = true;
                g_ForceRpcUpdate = false;
            }

            ULONGLONG now = GetTickCount64();
            if (forceRpcUpdate && isRpcEnabled) {
                if (now - lastDiscordUpdateTick >= 5000) {
                    if (hDiscordPipe == INVALID_HANDLE_VALUE) ConnectDiscord();
                    SetDiscordActivity(currentProjectName, parsedHours, parsedMinutes, sessionStartUnix);
                    lastDiscordUpdateTick = now;
                    forceRpcUpdate = false;
                }
            }
        }
        else {
            if (IsWindowVisible(hOverlay)) ShowWindow(hOverlay, SW_HIDE);
        }
        Sleep(20);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}