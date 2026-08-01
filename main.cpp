#ifndef UNICODE
#define UNICODE
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <random>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace Gdiplus;

struct Comment {
    std::wstring text;
    float x;
    float y;
    float speed;
    COLORREF color;
};

HINSTANCE g_hInstance = NULL;
HWND g_hHostJoinWnd = NULL;
HWND g_hOverlayWnd = NULL;
HWND g_hChatWnd = NULL;
HWND g_hSettingsWnd = NULL;

HWND g_hIpEdit = NULL, g_hPortEdit = NULL;
HWND g_hChatEdit = NULL;

ULONG_PTR g_gdiplusToken;
std::vector<Comment> g_comments;
std::mutex g_commentMutex;

SOCKET g_socket = INVALID_SOCKET;
bool g_isServer = false;
std::vector<SOCKET> g_clients;

float g_commentFontSize = 28.0f;
float g_commentBaseSpeed = 3.0f;

void DisconnectAndReturnToHostJoinUI() {
    if (g_isServer) {
        for (SOCKET client : g_clients) {
            closesocket(client);
        }
        g_clients.clear();
    }
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    WSACleanup();

    {
        std::lock_guard<std::mutex> lock(g_commentMutex);
        g_comments.clear();
    }

    ShowWindow(g_hChatWnd, SW_HIDE);
    ShowWindow(g_hOverlayWnd, SW_HIDE);
    ShowWindow(g_hHostJoinWnd, SW_SHOW);
}

WNDPROC g_oldEditProc = NULL;
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hwnd), WM_COMMAND, 201, 0);
        return 0;
    }
    return CallWindowProc(g_oldEditProc, hwnd, uMsg, wParam, lParam);
}

void SendNetworkMessage(const std::wstring& msg) {
    if (msg.empty()) return;

    std::string utf8Msg;
    int len = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, NULL, 0, NULL, NULL);
    if (len > 0) {
        utf8Msg.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, &utf8Msg[0], len, NULL, NULL);
    }

    if (g_isServer) {
        for (SOCKET client : g_clients) {
            send(client, utf8Msg.c_str(), (int)utf8Msg.size(), 0);
        }
    } else if (g_socket != INVALID_SOCKET) {
        send(g_socket, utf8Msg.c_str(), (int)utf8Msg.size(), 0);
    }

    std::lock_guard<std::mutex> lock(g_commentMutex);
    RECT rect;
    GetClientRect(g_hOverlayWnd, &rect);
    
    Comment c;
    c.text = msg;
    c.x = (float)rect.right;
    c.y = (float)(rand() % (rect.bottom - 150) + 30);
    c.speed = g_commentBaseSpeed + (rand() % 20) / 10.0f;
    c.color = RGB(255, 255, 255);
    g_comments.push_back(c);
}

void ListenThread() {
    char buffer[1024];
    while (true) {
        if (!g_isServer && g_socket == INVALID_SOCKET) break;

        if (g_isServer) {
            fd_set readfds;
            FD_ZERO(&readfds);
            for (SOCKET s : g_clients) FD_SET(s, &readfds);
            
            timeval timeout = { 1, 0 };
            if (select(0, &readfds, NULL, NULL, &timeout) > 0) {
                for (size_t i = 0; i < g_clients.size(); ++i) {
                    if (FD_ISSET(g_clients[i], &readfds)) {
                        int bytes = recv(g_clients[i], buffer, sizeof(buffer) - 1, 0);
                        if (bytes > 0) {
                            buffer[bytes] = '\0';
                            wchar_t wbuf[1024];
                            MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuf, 1024);
                            
                            std::lock_guard<std::mutex> lock(g_commentMutex);
                            RECT rect;
                            GetClientRect(g_hOverlayWnd, &rect);
                            Comment c = { wbuf, (float)rect.right, (float)(rand() % (rect.bottom - 150) + 30), g_commentBaseSpeed + (rand() % 20) / 10.0f, RGB(255, 255, 255) };
                            g_comments.push_back(c);

                            for (size_t j = 0; j < g_clients.size(); ++j) {
                                if (i != j) send(g_clients[j], buffer, bytes, 0);
                            }
                        }
                    }
                }
            }
        } else {
            int bytes = recv(g_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                wchar_t wbuf[1024];
                MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuf, 1024);

                std::lock_guard<std::mutex> lock(g_commentMutex);
                RECT rect;
                GetClientRect(g_hOverlayWnd, &rect);
                Comment c = { wbuf, (float)rect.right, (float)(rand() % (rect.bottom - 150) + 30), g_commentBaseSpeed + (rand() % 20) / 10.0f, RGB(255, 255, 255) };
                g_comments.push_back(c);
            } else {
                break;
            }
        }
    }
}

HWND g_hFontSizeEdit = NULL, g_hSpeedEdit = NULL;
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        CreateWindow(L"STATIC", L"My Font Size:", WS_CHILD | WS_VISIBLE, 20, 20, 100, 20, hwnd, NULL, g_hInstance, NULL);
        g_hFontSizeEdit = CreateWindow(L"EDIT", std::to_wstring((int)g_commentFontSize).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 130, 20, 90, 22, hwnd, NULL, g_hInstance, NULL);

        CreateWindow(L"STATIC", L"My Speed:", WS_CHILD | WS_VISIBLE, 20, 50, 100, 20, hwnd, NULL, g_hInstance, NULL);
        g_hSpeedEdit = CreateWindow(L"EDIT", std::to_wstring((int)g_commentBaseSpeed).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 130, 50, 90, 22, hwnd, NULL, g_hInstance, NULL);

        CreateWindow(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 90, 90, 30, hwnd, (HMENU)301, g_hInstance, NULL);
        CreateWindow(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 130, 90, 90, 30, hwnd, (HMENU)302, g_hInstance, NULL);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 301) {
            wchar_t sizeBuf[16], speedBuf[16];
            GetWindowText(g_hFontSizeEdit, sizeBuf, 16);
            GetWindowText(g_hSpeedEdit, speedBuf, 16);

            float sizeVal = (float)_wtof(sizeBuf);
            float speedVal = (float)_wtof(speedBuf);

            if (sizeVal >= 10.0f && sizeVal <= 100.0f) g_commentFontSize = sizeVal;
            if (speedVal >= 1.0f && speedVal <= 20.0f) g_commentBaseSpeed = speedVal;

            ShowWindow(hwnd, SW_HIDE);
        } else if (LOWORD(wParam) == 302) {
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OpenSettingsWindow(HWND hParent) {
    if (!g_hSettingsWnd) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = g_hInstance;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"SettingsClass";
        RegisterClass(&wc);

        g_hSettingsWnd = CreateWindow(L"SettingsClass", L"Viewer Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 260, 170, hParent, NULL, g_hInstance, NULL);
    }
    
    SetWindowText(g_hFontSizeEdit, std::to_wstring((int)g_commentFontSize).c_str());
    SetWindowText(g_hSpeedEdit, std::to_wstring((int)g_commentBaseSpeed).c_str());

    ShowWindow(g_hSettingsWnd, SW_SHOW);
    SetFocus(g_hSettingsWnd);
}

LRESULT CALLBACK ChatProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hChatEdit = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 220, 25, hwnd, (HMENU)101, g_hInstance, NULL);
        CreateWindowEx(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            240, 10, 50, 25, hwnd, (HMENU)201, g_hInstance, NULL);
        CreateWindowEx(0, L"BUTTON", L"Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            295, 10, 60, 25, hwnd, (HMENU)202, g_hInstance, NULL);
        CreateWindowEx(0, L"BUTTON", L"Disconnect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 10, 80, 25, hwnd, (HMENU)204, g_hInstance, NULL);
        CreateWindowEx(0, L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            445, 10, 45, 25, hwnd, (HMENU)203, g_hInstance, NULL);

        g_oldEditProc = (WNDPROC)SetWindowLongPtr(g_hChatEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 201) {
            wchar_t buf[256];
            GetWindowText(g_hChatEdit, buf, 256);
            if (wcslen(buf) > 0) {
                SendNetworkMessage(buf);
                SetWindowText(g_hChatEdit, L"");
            }
            SetFocus(g_hChatEdit);
        } else if (LOWORD(wParam) == 202) {
            OpenSettingsWindow(hwnd);
        } else if (LOWORD(wParam) == 204) {
            DisconnectAndReturnToHostJoinUI();
        } else if (LOWORD(wParam) == 203) {
            PostQuitMessage(0);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        SetTimer(hwnd, 1, 16, NULL);
        return 0;
    }
    case WM_TIMER: {
        RECT rect;
        GetClientRect(hwnd, &rect);

        std::lock_guard<std::mutex> lock(g_commentMutex);
        for (auto it = g_comments.begin(); it != g_comments.end();) {
            it->x -= it->speed;
            if (it->x < -1000) {
                it = g_comments.erase(it);
            } else {
                ++it;
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        SelectObject(memDC, hBitmap);

        {
            Graphics graphics(memDC);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.Clear(Color(0, 0, 0, 0));

            FontFamily fontFamily(L"Arial");
            Font font(&fontFamily, g_commentFontSize, FontStyleBold, UnitPixel);

            std::lock_guard<std::mutex> lock(g_commentMutex);
            for (const auto& c : g_comments) {
                GraphicsPath path;
                StringFormat format;
                path.AddString(c.text.c_str(), -1, &fontFamily, FontStyleBold, g_commentFontSize, PointF(c.x, c.y), &format);

                Pen pen(Color(255, 0, 0, 0), 4);
                pen.SetLineJoin(LineJoinRound);
                graphics.DrawPath(&pen, &path);

                SolidBrush brush(Color(255, 255, 255, 255));
                graphics.FillPath(&brush, &path);
            }
        }

        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        POINT ptSrc = { 0, 0 };
        SIZE sizeWnd = { rect.right, rect.bottom };
        UpdateLayeredWindow(hwnd, hdc, NULL, &sizeWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        DeleteObject(hBitmap);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK HostJoinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        CreateWindow(L"STATIC", L"IP:", WS_CHILD | WS_VISIBLE, 20, 20, 40, 20, hwnd, NULL, g_hInstance, NULL);
        g_hIpEdit = CreateWindow(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER, 70, 20, 200, 22, hwnd, NULL, g_hInstance, NULL);

        CreateWindow(L"STATIC", L"Port:", WS_CHILD | WS_VISIBLE, 20, 50, 40, 20, hwnd, NULL, g_hInstance, NULL);
        g_hPortEdit = CreateWindow(L"EDIT", L"12345", WS_CHILD | WS_VISIBLE | WS_BORDER, 70, 50, 200, 22, hwnd, NULL, g_hInstance, NULL);

        CreateWindow(L"BUTTON", L"Open Server (Host)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 85, 250, 30, hwnd, (HMENU)1, g_hInstance, NULL);
        CreateWindow(L"BUTTON", L"Connect (Join)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 120, 250, 30, hwnd, (HMENU)2, g_hInstance, NULL);

        CreateWindow(L"BUTTON", L"Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 160, 120, 25, hwnd, (HMENU)3, g_hInstance, NULL);
        CreateWindow(L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 150, 160, 120, 25, hwnd, (HMENU)4, g_hInstance, NULL);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 1 || LOWORD(wParam) == 2) {
            wchar_t ipBuf[64], portBuf[16];
            GetWindowText(g_hIpEdit, ipBuf, 64);
            GetWindowText(g_hPortEdit, portBuf, 16);
            int port = _wtoi(portBuf);

            char ipStr[64];
            WideCharToMultiByte(CP_UTF8, 0, ipBuf, -1, ipStr, 64, NULL, NULL);

            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);

            if (LOWORD(wParam) == 1) {
                g_isServer = true;
                SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
                sockaddr_in addr = { 0 };
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                addr.sin_addr.s_addr = INADDR_ANY;

                bind(serverSock, (sockaddr*)&addr, sizeof(addr));
                listen(serverSock, 5);

                std::thread([serverSock]() {
                    while (true) {
                        SOCKET client = accept(serverSock, NULL, NULL);
                        if (client != INVALID_SOCKET) {
                            g_clients.push_back(client);
                        }
                    }
                }).detach();
            } else {
                g_isServer = false;
                g_socket = socket(AF_INET, SOCK_STREAM, 0);
                sockaddr_in addr = { 0 };
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                inet_pton(AF_INET, ipStr, &addr.sin_addr);

                if (connect(g_socket, (sockaddr*)&addr, sizeof(addr)) != 0) {
                    MessageBox(hwnd, L"Failed to connect to server.", L"Error", MB_OK | MB_ICONERROR);
                    WSACleanup();
                    return 0;
                }
                std::thread(ListenThread).detach();
            }

            ShowWindow(hwnd, SW_HIDE);
            ShowWindow(g_hOverlayWnd, SW_SHOWMAXIMIZED);
            ShowWindow(g_hChatWnd, SW_SHOW);
            SetFocus(g_hChatEdit);
        } else if (LOWORD(wParam) == 3) {
            OpenSettingsWindow(hwnd);
        } else if (LOWORD(wParam) == 4) {
            PostQuitMessage(0);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInstance = hInstance;

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASS wc1 = { 0 };
    wc1.lpfnWndProc = HostJoinProc;
    wc1.hInstance = hInstance;
    wc1.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc1.lpszClassName = L"HostJoinClass";
    RegisterClass(&wc1);

    g_hHostJoinWnd = CreateWindow(L"HostJoinClass", L"NiconicoComment - Connect", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 310, 235, NULL, NULL, hInstance, NULL);

    WNDCLASS wc2 = { 0 };
    wc2.lpfnWndProc = OverlayProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"OverlayClass";
    RegisterClass(&wc2);

    g_hOverlayWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"OverlayClass", L"OverlayWindow", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );

    WNDCLASS wc3 = { 0 };
    wc3.lpfnWndProc = ChatProc;
    wc3.hInstance = hInstance;
    wc3.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc3.lpszClassName = L"ChatClass";
    RegisterClass(&wc3);

    g_hChatWnd = CreateWindowEx(
        WS_EX_TOPMOST, L"ChatClass", L"Comment Input", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, 510, 80, NULL, NULL, hInstance, NULL
    );

    ShowWindow(g_hHostJoinWnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(g_gdiplusToken);
    return 0;
}





