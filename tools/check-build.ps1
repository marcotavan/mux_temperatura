param([ValidateSet('esp32','esp32c3','esp32c6')][string]$Target = 'esp32c6')
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'safe.directory'
$env:GIT_CONFIG_VALUE_0 = 'C:/esp/v6.1/esp-idf'
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $PSScriptRoot '..\build-cache'
python "$env:IDF_PATH\tools\idf.py" -B "build-$Target" -D "IDF_TARGET=$Target" -D "SDKCONFIG=sdkconfig.$Target" build
exit $LASTEXITCODE
