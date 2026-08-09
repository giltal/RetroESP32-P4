# Resolve ESP-IDF + Python env paths from env vars, with auto-detection fallback.
# Dot-source this file, then call: Initialize-IdfEnv

function Find-NewestPath {
    param([string[]]$Patterns)
    foreach ($p in $Patterns) {
        $hit = Get-ChildItem -Path $p -Directory -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

function Initialize-IdfEnv {
    if (-not $env:IDF_PATH -or -not (Test-Path $env:IDF_PATH)) {
        $env:IDF_PATH = Find-NewestPath @(
            (Join-Path $env:USERPROFILE 'esp\*\esp-idf'),
            'C:\esp\*\esp-idf'
        )
    }
    if (-not $env:IDF_PATH) {
        throw "ESP-IDF not found. Set `$env:IDF_PATH or install under %USERPROFILE%\esp\<ver>\esp-idf."
    }

    if (-not $env:IDF_PYTHON_ENV_PATH -or -not (Test-Path $env:IDF_PYTHON_ENV_PATH)) {
        $env:IDF_PYTHON_ENV_PATH = Find-NewestPath @(
            (Join-Path $env:USERPROFILE '.espressif\python_env\idf*_env')
        )
    }
    if (-not $env:IDF_PYTHON_ENV_PATH) {
        throw "IDF Python env not found. Set `$env:IDF_PYTHON_ENV_PATH or run the IDF installer."
    }

    Write-Host "IDF_PATH            = $env:IDF_PATH"
    Write-Host "IDF_PYTHON_ENV_PATH = $env:IDF_PYTHON_ENV_PATH"

    $export = Join-Path $env:IDF_PATH 'export.ps1'
    if (-not (Test-Path $export)) { throw "export.ps1 missing at $export" }

    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $export 2>$null
    $ErrorActionPreference = $prev
}
