#!/bin/sh
set -eu
# Run inside flatpak build with org.kde.Sdk 6.10 and the openjdk17 extension.
export PATH=/usr/lib/sdk/openjdk17/bin:$PATH
export CMAKE_PREFIX_PATH=/app:/app/lib/x86_64-linux-gnu/cmake
export PKG_CONFIG_PATH=/app/lib/pkgconfig:/app/lib/x86_64-linux-gnu/pkgconfig
cd /home/deck/Applications/prism-console-source
mkdir -p deps
if [ ! -d deps/cmark ]; then git clone --depth 1 --branch 0.31.1 https://github.com/commonmark/cmark.git deps/cmark; fi
cmake -S deps/cmark -B deps/cmark-build -G Ninja -DCMAKE_INSTALL_PREFIX=/app -DCMARK_TESTS=OFF -DCMARK_STATIC=OFF
cmake --build deps/cmark-build -j4
cmake --install deps/cmark-build
if [ ! -d deps/tomlplusplus ]; then git clone --depth 1 --branch v3.4.0 https://github.com/marzer/tomlplusplus.git deps/tomlplusplus; fi
cmake -S deps/tomlplusplus -B deps/toml-build -G Ninja -DCMAKE_INSTALL_PREFIX=/app -DTOML_BUILD_TESTS=OFF -DTOML_BUILD_EXAMPLES=OFF
cmake --build deps/toml-build -j4
cmake --install deps/toml-build
cmake -S . -B steam-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/app -DLauncher_CONSOLE_UI=ON -DLauncher_ENABLE_UPDATER=OFF -DBUILD_TESTING=OFF -DLauncher_CURSEFORGE_API_KEY= -DLauncher_MSA_CLIENT_ID="${PRISM_CONSOLE_MSA_CLIENT_ID:-}" -DJava_JAVAC_EXECUTABLE=/usr/lib/sdk/openjdk17/bin/javac -DJava_JAVA_EXECUTABLE=/usr/lib/sdk/openjdk17/bin/java
cmake --build steam-build -j4
cmake --install steam-build
echo STEAMOS-BUILD-DONE
