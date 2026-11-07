# Compila storm su Windows con gcc di MSYS2 UCRT64 e cmake/ninja del PSn00bSDK.
# Il PATH cambia solo dentro questo script. Uso:  powershell -ExecutionPolicy Bypass -File build.ps1
$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\ucrt64\bin;$env:LOCALAPPDATA\Programs\PSn00bSDK\cmake\bin;$env:LOCALAPPDATA\Programs\PSn00bSDK\sdk\bin;$env:PATH"
Set-Location $PSScriptRoot
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build build
exit $LASTEXITCODE
