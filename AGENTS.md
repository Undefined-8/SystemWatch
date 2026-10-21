# Repository Guidelines

## Project Structure

This is a Windows desktop system-monitor overlay built with Qt 5.9.6 and C++11. Application sources and headers live in the repository root: `main.cpp` initializes the app, `systemmonitor.*` collects system data, `overlay.*` renders the overlay, and `mainwindow.*` implements settings. Icons and Qt resources are in `res/` and `style.qrc`. `debug/` and `release/` contain generated build outputs; do not treat generated files as source. No automated test directory or test framework is currently configured.

## Build and Run

Open `system_watch.pro` in Qt Creator and select the Qt 5.9.6 MSVC 2015 kit. From a Qt/MSVC 2015 developer environment, configure and build with:

```bat
qmake system_watch.pro
nmake /Y /f Makefile.Debug
```

Run `debug\system_watch.exe` with the Qt MSVC 2015 `bin` directory available on `PATH`. `/Y` asks NMAKE to build inference-rule targets one at a time, avoiding the multi-source compiler invocation seen with this project.

## Coding Style and Architecture

Use four-space indentation and the existing naming patterns: PascalCase class names (`SystemMonitor`), camelCase methods (`updateData`), and `m_`-prefixed member fields (`m_cpuTemp`). Keep code compatible with C++11 and Qt 5.9.6. Guard Windows-specific APIs with `Q_OS_WIN32`. Keep periodic collection on the monitor worker thread; protect shared readings with the existing mutex. Initialize and release COM interfaces on the appropriate thread, and pair successful COM initialization with `CoUninitialize`.

## Testing

There is no configured automated test suite. After a change, build with the command above and manually check startup, overlay visibility and settings, sensor-present and sensor-missing behavior, plus network and storage rows. For UI changes, verify that long values remain readable and capture a screenshot for review.

## Commits and Pull Requests

No Git history is available in this checkout to establish existing commit conventions. Use concise Conventional Commit prefixes such as `feat:` or `fix:`. PRs should describe user-visible behavior, include build commands and results, link related issues when applicable, and attach screenshots for visual changes.

## Hardware Sensor Dependencies

Intel CPU temperature may be read from the optional LibreHardwareMonitor WMI provider when it is running. Do not label an unrelated ACPI thermal-zone reading as CPU temperature; show the reading as unavailable when no CPU sensor is exposed.
