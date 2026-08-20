$rimeDir = Join-Path $env:APPDATA 'Rime'
New-Item -ItemType Directory -Path $rimeDir -Force | Out-Null
Start-Process -FilePath 'explorer.exe' -ArgumentList $rimeDir
