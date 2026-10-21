#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QDebug>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include "systemmonitor.h"
#include "overlay.h"
#include "mainwindow.h"
#include "hotkeyfilter.h"

static HWND g_mainWindowHandle = nullptr;

#ifdef Q_OS_WIN32
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_HOTKEY && wParam == 1) {
        if (g_mainWindowHandle) {
            PostMessage(g_mainWindowHandle, WM_USER + 1, 0, 0);
        }
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Game Monitor");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("GameMonitor");
    app.setWindowIcon(QIcon(":/res/icon.png"));

#ifdef Q_OS_WIN32
    HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        qDebug() << "COM init failed, error:" << hr;
    }

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GameMonitorHotkeyClass";

    if (!RegisterClassEx(&wc)) {
        qDebug() << "Window class registration failed";
    }

    HWND hotkeyWindow = CreateWindowEx(
        0,
        L"GameMonitorHotkeyClass",
        L"HotkeyWindow",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!hotkeyWindow) {
        qDebug() << "Hotkey window creation failed";
    }
#endif

    SystemMonitor *monitor = new SystemMonitor();
    Overlay *overlay = new Overlay(monitor);
    MainWindow *mainWindow = new MainWindow(monitor, overlay);

#ifdef Q_OS_WIN32
    g_mainWindowHandle = (HWND)mainWindow->winId();
#endif

    HotkeyFilter *hotkeyFilter = new HotkeyFilter();
    hotkeyFilter->mainWindow = mainWindow;
    app.installEventFilter(hotkeyFilter);

    mainWindow->show();
    overlay->showOverlay();

    int ret = app.exec();

    delete mainWindow;
    delete overlay;
    delete monitor;

#ifdef Q_OS_WIN32
    UnregisterHotKey(hotkeyWindow, 1);
    if (hotkeyWindow) {
        DestroyWindow(hotkeyWindow);
    }
    UnregisterClass(L"GameMonitorHotkeyClass", GetModuleHandle(NULL));
    CoUninitialize();
#endif

    return ret;
}
