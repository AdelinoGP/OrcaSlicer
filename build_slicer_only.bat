@echo off
set WP=%CD%
set debug=OFF
set debuginfo=OFF
set arch=x64
set build_type=RelWithDebInfo
set build_dir=build-dbginfo

cd deps
cd %build_dir%
echo "skipping deps build (already done)"

cd %WP%
mkdir %build_dir%
cd %build_dir%

set CMAKE_POLICY_VERSION_MINIMUM=3.5
"C:\Program Files\CMake\bin\cmake.exe" .. -G "Visual Studio 18 2026" -A x64 -DORCA_TOOLS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
"C:\Program Files\CMake\bin\cmake.exe" --build . --config RelWithDebInfo --target ALL_BUILD -- -m
cd ..
call scripts\run_gettext.bat
cd %build_dir%
"C:\Program Files\CMake\bin\cmake.exe" --build . --target install --config RelWithDebInfo
