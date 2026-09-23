#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <gl/GL.h>
#include <cmath>
#include <cwchar>
#include <vector>
#include <algorithm>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")

struct Vec3 { double x, y, z; };
struct Line { Vec3 a, b; };

static HWND gWindow = nullptr;
static HDC gDc = nullptr;
static HGLRC gGl = nullptr;
static int gWidth = 1000, gHeight = 700;
static double gYaw = -35.0, gPitch = 35.0, gZoom = 1.0;
static double gPanX = 0.0, gPanY = 0.0;
static bool gLeftDown = false, gRightDown = false, gMiddleDown = false;
static POINT gLastMouse{};
static std::vector<Line> gLines;
static std::vector<std::vector<Line>> gUndo, gRedo;
static bool gHasFirstPoint = false;
static Vec3 gFirstPoint{};

static void pushUndo() { gUndo.push_back(gLines); gRedo.clear(); }

static void applyCamera() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = gHeight > 0 ? static_cast<double>(gWidth) / gHeight : 1.0;
    const double half = 240.0 / gZoom;
    glOrtho(-half * aspect + gPanX, half * aspect + gPanX,
            -half + gPanY, half + gPanY, -1000.0, 1000.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotated(-gPitch, 1.0, 0.0, 0.0);
    glRotated(-gYaw, 0.0, 0.0, 1.0);
}

static void drawGrid() {
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = -200; i <= 200; i += 10) {
        const float shade = i == 0 ? 0.25f : 0.10f;
        glColor3f(shade, shade, shade);
        glVertex3d(i, -200, 0); glVertex3d(i, 200, 0);
        glVertex3d(-200, i, 0); glVertex3d(200, i, 0);
    }
    glEnd();
    glLineWidth(2.5f);
    glBegin(GL_LINES);
    glColor3f(0.85f, 0.18f, 0.15f); glVertex3d(-200, 0, 0); glVertex3d(200, 0, 0);
    glColor3f(0.18f, 0.75f, 0.25f); glVertex3d(0, -200, 0); glVertex3d(0, 200, 0);
    glColor3f(0.20f, 0.45f, 0.95f); glVertex3d(0, 0, -80); glVertex3d(0, 0, 80);
    glEnd();
}

static void drawScene() {
    glClearColor(0.035f, 0.045f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    applyCamera();
    drawGrid();
    glLineWidth(4.0f);
    glColor3f(1.0f, 0.82f, 0.18f);
    glBegin(GL_LINES);
    for (const auto& line : gLines) {
        glVertex3d(line.a.x, line.a.y, line.a.z);
        glVertex3d(line.b.x, line.b.y, line.b.z);
    }
    glEnd();
    if (gHasFirstPoint) {
        glLineWidth(2.0f);
        glColor3f(0.95f, 0.95f, 0.95f);
        glBegin(GL_LINES);
        glVertex3d(gFirstPoint.x, gFirstPoint.y, 0);
        glVertex3d(gFirstPoint.x + 0.01, gFirstPoint.y + 0.01, 0);
        glEnd();
    }
    SwapBuffers(gDc);
}

static Vec3 screenToPlane(int x, int y) {
    const double aspect = gHeight > 0 ? static_cast<double>(gWidth) / gHeight : 1.0;
    const double half = 240.0 / gZoom;
    return {(x / static_cast<double>(gWidth) - 0.5) * 2.0 * half * aspect + gPanX,
            (0.5 - y / static_cast<double>(gHeight)) * 2.0 * half + gPanY, 0.0};
}

static void setTitle() {
    wchar_t title[256];
    std::swprintf(title, 256, L"3D Line Viewer | LMB: create line  RMB: orbit  MMB: pan  Wheel: zoom | Lines: %llu | Ctrl+Z/Y",
                   static_cast<unsigned long long>(gLines.size()));
    SetWindowTextW(gWindow, title);
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        gWidth = std::max(1, static_cast<int>(LOWORD(lParam)));
        gHeight = std::max(1, static_cast<int>(HIWORD(lParam)));
        glViewport(0, 0, gWidth, gHeight);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd); gLeftDown = true;
        gLastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!gHasFirstPoint) { gFirstPoint = screenToPlane(gLastMouse.x, gLastMouse.y); gHasFirstPoint = true; }
        else { pushUndo(); gLines.push_back({gFirstPoint, screenToPlane(gLastMouse.x, gLastMouse.y)}); gHasFirstPoint = false; setTitle(); }
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_RBUTTONDOWN: SetCapture(hwnd); gRightDown = true; gLastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; return 0;
    case WM_MBUTTONDOWN: SetCapture(hwnd); gMiddleDown = true; gLastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        gLeftDown = gRightDown = gMiddleDown = false; ReleaseCapture(); return 0;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int dx = p.x - gLastMouse.x, dy = p.y - gLastMouse.y; gLastMouse = p;
        if (gRightDown) { gYaw += dx * 0.5; gPitch = std::clamp(gPitch + dy * 0.5, -89.0, 89.0); InvalidateRect(hwnd, nullptr, FALSE); }
        if (gMiddleDown) { gPanX -= dx * 0.5 / gZoom; gPanY += dy * 0.5 / gZoom; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0; }
    case WM_MOUSEWHEEL:
        gZoom = std::clamp(gZoom * (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.15 : 1.0 / 1.15), 0.08, 40.0); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z' && !gUndo.empty()) { gRedo.push_back(gLines); gLines = gUndo.back(); gUndo.pop_back(); setTitle(); InvalidateRect(hwnd, nullptr, FALSE); }
        else if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y' && !gRedo.empty()) { gUndo.push_back(gLines); gLines = gRedo.back(); gRedo.pop_back(); setTitle(); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wParam == VK_ESCAPE) { gHasFirstPoint = false; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); drawScene(); EndPaint(hwnd, &ps); return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = windowProc; wc.lpszClassName = L"Homework3DViewer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_OWNDC;
    RegisterClassW(&wc);
    gWindow = CreateWindowExW(0, wc.lpszClassName, L"3D Line Viewer", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, gWidth, gHeight, nullptr, nullptr, instance, nullptr);
    gDc = GetDC(gWindow);
    PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize = sizeof(pfd); pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 24; pfd.cDepthBits = 24;
    SetPixelFormat(gDc, ChoosePixelFormat(gDc, &pfd), &pfd);
    gGl = wglCreateContext(gDc); wglMakeCurrent(gDc, gGl);
    ShowWindow(gWindow, show); setTitle();
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(gGl); ReleaseDC(gWindow, gDc);
    return static_cast<int>(msg.wParam);
}
