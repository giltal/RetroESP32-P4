@echo off
REM resolve_idf_env.bat -- Resolve ESP-IDF + Python env paths and source export.bat.
REM Honors IDF_PATH and IDF_PYTHON_ENV_PATH if set; otherwise picks the newest
REM matching install under %USERPROFILE%\esp\* and %USERPROFILE%\.espressif\python_env\*.
REM
REM Usage:  call "<path>\tools\resolve_idf_env.bat"  || exit /b 1

REM --- ESP-IDF -----------------------------------------------------------
if defined IDF_PATH if exist "%IDF_PATH%\export.bat" goto :idf_ok

set "IDF_PATH="
for /f "delims=" %%D in ('dir /b /ad /o-d "%USERPROFILE%\esp\*" 2^>nul') do (
    if not defined IDF_PATH if exist "%USERPROFILE%\esp\%%D\esp-idf\export.bat" (
        set "IDF_PATH=%USERPROFILE%\esp\%%D\esp-idf"
    )
)
if not defined IDF_PATH (
    for /f "delims=" %%D in ('dir /b /ad /o-d "C:\esp\*" 2^>nul') do (
        if not defined IDF_PATH if exist "C:\esp\%%D\esp-idf\export.bat" (
            set "IDF_PATH=C:\esp\%%D\esp-idf"
        )
    )
)
if not defined IDF_PATH (
    echo ERROR: ESP-IDF not found. Set IDF_PATH or install under %%USERPROFILE%%\esp\^<ver^>\esp-idf.
    exit /b 1
)

:idf_ok

REM --- Python env (must be set BEFORE export.bat runs) --------------------
if defined IDF_PYTHON_ENV_PATH if exist "%IDF_PYTHON_ENV_PATH%" goto :py_ok

set "IDF_PYTHON_ENV_PATH="
for /f "delims=" %%D in ('dir /b /ad /o-d "%USERPROFILE%\.espressif\python_env\idf*_env" 2^>nul') do (
    if not defined IDF_PYTHON_ENV_PATH (
        set "IDF_PYTHON_ENV_PATH=%USERPROFILE%\.espressif\python_env\%%D"
    )
)
if not defined IDF_PYTHON_ENV_PATH (
    echo ERROR: IDF Python env not found. Set IDF_PYTHON_ENV_PATH or run the IDF installer.
    exit /b 1
)

:py_ok

echo IDF_PATH            = %IDF_PATH%
echo IDF_PYTHON_ENV_PATH = %IDF_PYTHON_ENV_PATH%

call "%IDF_PATH%\export.bat"
exit /b %ERRORLEVEL%
