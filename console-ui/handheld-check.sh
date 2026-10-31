#!/bin/sh
set -eu
export QT_QPA_PLATFORM=xcb
export QT_XCB_GL_INTEGRATION=none
export QT_QUICK_BACKEND=software
export QT_OPENGL=software
export LIBGL_ALWAYS_SOFTWARE=1
export PRISM_CONSOLE=1
export QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222
export QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu
export QTWEBENGINEPROCESS_PATH=/app/bin/QtWebEngineProcess
export QTWEBENGINE_RESOURCES_PATH=/app/resources
export QTWEBENGINE_LOCALES_PATH=/app/translations/qtwebengine_locales
export LD_LIBRARY_PATH=/app/lib:/app/lib64:/app/lib/x86_64-linux-gnu
exec /app/bin/prismlauncher --dir /home/deck/Applications/prism-console-smoke-data
