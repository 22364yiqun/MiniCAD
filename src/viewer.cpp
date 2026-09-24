#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <gl/GL.h>
#include <cmath>
#include <cwchar>
#include <vector>
#include <algorithm>
#include <limits>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")

struct Vec3 { double x, y, z; };
struct Line { Vec3 a, b; };

static bool operator==(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

static bool operator==(const Line& lhs, const Line& rhs) {
    return lhs.a == rhs.a && lhs.b == rhs.b;
}

static HWND gWindow = nullptr;
static HDC gDc = nullptr;
static HGLRC gGl = nullptr;
static int gWidth = 1000, gHeight = 700;
static double gZoom = 1.0;
static double gPanX = 0.0, gPanY = 0.0;
static bool gLeftDown = false, gRightDown = false, gMiddleDown = false;
static POINT gLastMouse{};
static std::vector<Line> gLines;
static std::vector<std::vector<Line>> gUndo, gRedo;
static bool gDraggingLine = false;
static bool gRotatingLine = false;
static std::size_t gDraggedIndex = 0;
static Vec3 gDragAnchor{};
static Vec3 gRotateCenter{};
static double gRotateStartAngle = 0.0;
static std::vector<Line> gDragBefore;
static bool gHasFirstPoint = false;
static Vec3 gFirstPoint{};

static void pushUndo() { gUndo.push_back(gLines); gRedo.clear(); }

static double distanceSquaredToLine(const Line& line, Vec3 point) {
    const double dx = line.b.x - line.a.x;
    const double dy = line.b.y - line.a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-12) {
        const double px = point.x - line.a.x;
        const double py = point.y - line.a.y;
        return px * px + py * py;
    }
    const double t = std::clamp(((point.x - line.a.x) * dx + (point.y - line.a.y) * dy) /
                                     lengthSquared,
                                 0.0, 1.0);
    const double nearestX = line.a.x + t * dx;
    const double nearestY = line.a.y + t * dy;
    const double px = point.x - nearestX;
    const double py = point.y - nearestY;
    return px * px + py * py;
}

static Vec3 rotateAround(Vec3 point, Vec3 center, double radians) {
    const double x = point.x - center.x;
    const double y = point.y - center.y;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return {center.x + x * c - y * s, center.y + x * s + y * c, 0.0};
}

static int findLineAt(Vec3 point) {
    const double worldTolerance = 8.0 / gZoom;
    const double threshold = worldTolerance * worldTolerance;
    double bestDistance = threshold;
    int bestIndex = -1;
    for (std::size_t i = 0; i < gLines.size(); ++i) {
        const double distance = distanceSquaredToLine(gLines[i], point);
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

static void applyCamera() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = gHeight > 0 ? static_cast<double>(gWidth) / gHeight : 1.0;
    const double half = 240.0 / gZoom;
    glOrtho(-half * aspect + gPanX, half * aspect + gPanX,
            -half + gPanY, half + gPanY, -1000.0, 1000.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
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
    glEnd();
}

static void drawScene() {
    glClearColor(0.035f, 0.045f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    applyCamera();
    drawGrid();
    glLineWidth(4.0f);
    glBegin(GL_LINES);
    for (std::size_t i = 0; i < gLines.size(); ++i) {
        const auto& line = gLines[i];
        if (gDraggingLine && i == gDraggedIndex) glColor3f(0.25f, 1.0f, 0.45f);
        else glColor3f(1.0f, 0.82f, 0.18f);
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
    std::swprintf(title, 256, L"2D Line Viewer | LMB create/move  Shift+LMB rotate  RMB/MMB pan  Wheel zoom | Lines: %llu | Ctrl+Z/Y",
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
        {
            const Vec3 point = screenToPlane(gLastMouse.x, gLastMouse.y);
            const int hit = findLineAt(point);
            if (hit >= 0) {
                gDraggingLine = true;
                gRotatingLine = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                gDraggedIndex = static_cast<std::size_t>(hit);
                gDragAnchor = point;
                gDragBefore = gLines;
                if (gRotatingLine) {
                    const auto& line = gLines[gDraggedIndex];
                    gRotateCenter = {(line.a.x + line.b.x) * 0.5,
                                     (line.a.y + line.b.y) * 0.5, 0.0};
                    gRotateStartAngle = std::atan2(point.y - gRotateCenter.y,
                                                   point.x - gRotateCenter.x);
                }
            } else if (!gHasFirstPoint) {
                gFirstPoint = point;
                gHasFirstPoint = true;
            } else {
                pushUndo();
                gLines.push_back({gFirstPoint, point});
                gHasFirstPoint = false;
                setTitle();
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_RBUTTONDOWN: SetCapture(hwnd); gRightDown = true; gLastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; return 0;
    case WM_MBUTTONDOWN: SetCapture(hwnd); gMiddleDown = true; gLastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; return 0;
    case WM_LBUTTONUP:
        if (gDraggingLine) {
            if (gLines != gDragBefore) { gUndo.push_back(gDragBefore); gRedo.clear(); }
            gDraggingLine = false;
            gRotatingLine = false;
            setTitle();
        }
        gLeftDown = false; ReleaseCapture(); return 0;
    case WM_RBUTTONUP: case WM_MBUTTONUP:
        gRightDown = gMiddleDown = false; ReleaseCapture(); return 0;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int dx = p.x - gLastMouse.x, dy = p.y - gLastMouse.y; gLastMouse = p;
        if (gDraggingLine && gLeftDown) {
            const Vec3 point = screenToPlane(p.x, p.y);
            if (gRotatingLine) {
                const double angle = std::atan2(point.y - gRotateCenter.y,
                                                point.x - gRotateCenter.x);
                const double deltaAngle = angle - gRotateStartAngle;
                gLines[gDraggedIndex].a = rotateAround(gDragBefore[gDraggedIndex].a,
                                                       gRotateCenter, deltaAngle);
                gLines[gDraggedIndex].b = rotateAround(gDragBefore[gDraggedIndex].b,
                                                       gRotateCenter, deltaAngle);
            } else {
                const Vec3 delta{point.x - gDragAnchor.x, point.y - gDragAnchor.y, 0.0};
                gLines[gDraggedIndex].a = {gDragBefore[gDraggedIndex].a.x + delta.x,
                                           gDragBefore[gDraggedIndex].a.y + delta.y, 0.0};
                gLines[gDraggedIndex].b = {gDragBefore[gDraggedIndex].b.x + delta.x,
                                           gDragBefore[gDraggedIndex].b.y + delta.y, 0.0};
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (gRightDown) { gPanX -= dx * 0.5 / gZoom; gPanY += dy * 0.5 / gZoom; InvalidateRect(hwnd, nullptr, FALSE); }
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
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = windowProc; wc.lpszClassName = L"Homework2DViewer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_OWNDC;
    RegisterClassW(&wc);
    gWindow = CreateWindowExW(0, wc.lpszClassName, L"2D Line Viewer", WS_OVERLAPPEDWINDOW,
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
