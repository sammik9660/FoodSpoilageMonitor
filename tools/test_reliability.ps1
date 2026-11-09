param([switch]$SkipNative)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  foreach($test in @('apps_script_syntax_test.cjs','cloud.test.cjs','monitoring.test.cjs','remote_updates.test.cjs','remote_storage_test.mjs')) {
    & node "tests/$test"; if($LASTEXITCODE -ne 0){throw "Failed: $test"}
  }
  foreach($test in @('fingerprint_test.py','dataset_test.py','nrf_dfu_web_test.py','nrf_dfu_upload_test.py','gateway_update_access_test.py','ota_partition_test.py','partition_candidate_test.py','wifi_portal_upload_test.py')) {
    & python "tests/$test"; if($LASTEXITCODE -ne 0){throw "Failed: $test"}
  }
  if(!$SkipNative) {
    $vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$vs){throw 'MSVC C++ tools required; use -SkipNative only to explicitly skip host C++ checks.'}
    $out=Join-Path $root ('data/reliability-tests-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $out | Out-Null
    $lines=@('@echo off',('call "'+$vs+'\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul'))
    foreach($name in @('sensor_test','sensor_timing_test','sensor_progress_test','battery_test','integrity_test','spool_test','gateway_ui_test','gateway_ui_lcd_off_test','wifi_recovery_test','nrf_legacy_dfu_test','nrf_dfu_package_policy_test','remote_client_test','remote_runtime_test','tls_allocator_test')) {
      $include=if($name -eq 'spool_test'){'tests/spool_stubs'}elseif($name -like 'remote*'){'tests/remote_stubs'}else{'tests/sensor_stubs'}
      $lines+=('cl /nologo /utf-8 /std:c++17 /EHsc /I"'+$include+'" "tests/'+$name+'.cpp" /Fo:"'+$out+'/'+$name+'.obj" /Fe:"'+$out+'/'+$name+'.exe"')
      $lines+='if not "%errorlevel%"=="0" exit /b %errorlevel%'
      $lines+=('"'+$out+'/'+$name+'.exe"')
      $lines+='if not "%errorlevel%"=="0" exit /b %errorlevel%'
    }
    $cmd=Join-Path $out 'run.cmd'; Set-Content -LiteralPath $cmd -Value $lines -Encoding ascii
    & cmd.exe /c $cmd; if($LASTEXITCODE -ne 0){throw 'Native regression failed'}
  }
  & git diff --check; if($LASTEXITCODE -ne 0){throw 'Whitespace check failed'}
} finally {Pop-Location}
