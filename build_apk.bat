@echo off
REM Build the native arm64 library, copy it into the Gradle project, and package the APK.
set "REPO=%~dp0"
set "BUILD=%REPO%out\build\arm64-android"
set "NDK=%LOCALAPPDATA%\Android\Sdk\ndk\29.0.14206865"

if not exist "%NDK%\build\cmake\android.toolchain.cmake" (
    echo ERROR: Android NDK 29.0.14206865 was not found at "%NDK%".
    exit /b 1
)
set "ANDROID_NDK_HOME=%NDK%"
set "ANDROID_NDK_ROOT=%NDK%"

cmake --build "%BUILD%" --target UnleashedRecomp --parallel 4
if errorlevel 1 exit /b %errorlevel%

if not exist "%BUILD%\UnleashedRecomp\libmain.so" (
    echo ERROR: native library was not produced.
    exit /b 1
)
copy /Y "%BUILD%\UnleashedRecomp\libmain.so" "%REPO%android-apk\app\src\main\jniLibs\arm64-v8a\libmain.so"
if errorlevel 1 exit /b %errorlevel%

cd /d "%REPO%android-apk"
call gradlew.bat assembleDebug
if errorlevel 1 exit /b %errorlevel%
copy /Y "app\build\outputs\apk\debug\app-debug.apk" "app\build\outputs\apk\debug\UnleashedRecomp-0.6.0.apk"
exit /b %errorlevel%
