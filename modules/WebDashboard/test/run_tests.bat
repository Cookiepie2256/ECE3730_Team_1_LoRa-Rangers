@echo off
rem Host tests for WebDashboard (no ESP32 needed). Needs g++ and Python 3.
rem   test\run_tests.bat
setlocal
cd /d "%~dp0.."
set OUT=test\build
if not exist %OUT% mkdir %OUT%
del /q %OUT%\*.json 2>nul

python tools\embed_page.py --check || exit /b 1
g++ -std=c++17 -Wall -Wextra -Werror -I test/stubs -I . test/test_webdashboard.cpp WebDashboard.cpp -o %OUT%\test_webdashboard.exe || exit /b 1
%OUT%\test_webdashboard.exe %OUT% || exit /b 1

for %%f in (%OUT%\*.json) do (
    python -m json.tool "%%f" >nul || (echo invalid JSON: %%f & exit /b 1)
)
echo JSON check: every file is valid (python -m json.tool)
python test\test_live_feed.py || exit /b 1
echo All tests passed
