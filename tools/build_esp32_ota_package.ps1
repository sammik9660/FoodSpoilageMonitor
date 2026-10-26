param([string]$Version, [string]$ArduinoCli, [ValidateSet('Auto','Manual')][string]$Mode)
$ErrorActionPreference = 'Stop'

function Get-ReleasePlan([string]$Text, [string]$RequestedVersion, [UInt64[]]$PriorBuilds, [datetime]$Today = (Get-Date)) {
  $versions = [regex]::Matches($Text, '(?m)^#define FSGW_VERSION "(\d+\.\d+\.\d+)"\r?$')
  $builds = [regex]::Matches($Text, '(?m)^#define FSGW_BUILD (\d{10})\r?$')
  if ($versions.Count -ne 1 -or $builds.Count -ne 1) { throw 'Source identity is missing or ambiguous.' }
  $oldVersion = $versions[0].Groups[1].Value
  $oldBuild = [UInt64]$builds[0].Groups[1].Value
  if (!$RequestedVersion) {
    $v = [version]$oldVersion
    $RequestedVersion = "$($v.Major).$($v.Minor).$($v.Build + 1)"
  }
  if ($RequestedVersion -notmatch '^\d+\.\d+\.\d+$' -or [version]$RequestedVersion -lt [version]$oldVersion) {
    throw 'Choose a valid major.minor.patch version equal to or greater than the current source version. Use a prior signed package for a real firmware downgrade.'
  }
  $resumeBuild = $RequestedVersion -eq $oldVersion -and $PriorBuilds -notcontains $oldBuild
  $maximum = $oldBuild
  foreach ($prior in $PriorBuilds) { if ($prior -gt $maximum) { $maximum = $prior } }
  if($resumeBuild){$next=$oldBuild}else{$next = [Math]::Max([UInt64]($Today.ToString('yyyyMMdd') + '01'), $maximum + 1)}
  # Keep a valid YYYYMMDDNN even when NN=99 is exhausted or local clock moved backwards.
  if (!$resumeBuild -and $next.ToString().EndsWith('00')) {
    $date = [datetime]::ParseExact($maximum.ToString().Substring(0,8), 'yyyyMMdd', $null)
    $next = [UInt64]($date.AddDays(1).ToString('yyyyMMdd') + '01')
  }
  if ($next.ToString().Length -ne 10) { throw 'Build number exceeds YYYYMMDDNN.' }
  $changed = $Text.Replace($versions[0].Value, $versions[0].Value.Replace($oldVersion,$RequestedVersion))
  $changed = $changed.Replace($builds[0].Value, $builds[0].Value.Replace($oldBuild.ToString(),$next.ToString()))
  [pscustomobject]@{ Version=$RequestedVersion; Build=$next.ToString(); OldVersion=$oldVersion; OldBuild=$oldBuild; Text=$changed }
}

function Invoke-VisibleCompile([string]$Cli,[string]$BuildDir,[string]$Sketch,[string]$Fqbn) {
  $out=Join-Path $BuildDir 'compile.stdout.log';$err=Join-Path $BuildDir 'compile.stderr.log'
  $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Cli;$info.UseShellExecute=$false
  $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.CreateNoWindow=$true
  foreach($arg in @('compile','--fqbn',$Fqbn,'--build-path',$BuildDir,$Sketch)){$info.ArgumentList.Add($arg)}
  $process=[Diagnostics.Process]::new();$process.StartInfo=$info
  if(!$process.Start()){throw 'Could not start Arduino CLI.'}
  $stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync()
  $started=[datetime]::UtcNow;$spinner=@('|','/','-','\');$frame=0;$objects=0
  while(!$process.WaitForExit(900)){
    $elapsed=[int]([datetime]::UtcNow-$started).TotalSeconds
    if($frame%5 -eq 0){try{$objects=@(Get-ChildItem (Join-Path $BuildDir 'sketch') -Recurse -Filter '*.o' -ErrorAction SilentlyContinue).Count}catch{}}
    Write-Host -NoNewline ("`r컴파일 중 / Compiling {0}  {1:mm\:ss} elapsed · objects {2}  " -f $spinner[$frame%4],[timespan]::FromSeconds($elapsed),$objects)
    $frame++
  }
  $process.WaitForExit();Write-Host ''
  $stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult()
  [IO.File]::WriteAllText($out,$stdout);[IO.File]::WriteAllText($err,$stderr)
  if($process.ExitCode -ne 0){throw "Compilation failed (exit $($process.ExitCode)). See local logs under $BuildDir; no package was created."}
}

function Invoke-Release([string]$Root, [string]$RequestedVersion, [string]$Cli, [string]$Mode) {
  $source = Join-Path $Root 'firmware/esp32s3/gateway/gateway.ino'
  $key = Join-Path $Root 'secrets/esp32-ota-signing-key.pem'
  if (!(Test-Path -LiteralPath $key)) { throw 'Existing signing key missing; no source was changed.' }
  if ($Mode -eq 'Auto' -and !$Cli) {
    $command = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($command) { $Cli = $command.Source }
    else { $Cli = Join-Path $env:LOCALAPPDATA 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' }
  }
  if ($Mode -eq 'Auto') {
    if (!(Test-Path -LiteralPath $Cli)) { throw 'arduino-cli not found; install Arduino CLI or Arduino IDE first.' }
    $cores = & $Cli core list --format json | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or !(@($cores.platforms) | Where-Object { $_.id -eq 'esp32:esp32' -and $_.installed_version -eq '3.3.12' })) {
      throw 'ESP32 Arduino core 3.3.12 is required; no automatic core changes are performed.'
    }
  }
  $text = [IO.File]::ReadAllText($source)
  $prior = @()
  foreach ($base in @('data','dist')) {
    $folder = Join-Path $Root $base
    if (Test-Path $folder) {
      foreach ($file in Get-ChildItem $folder -Filter '*.manifest' -Recurse -File) {
        $m = [regex]::Match([IO.File]::ReadAllText($file.FullName),'(?m)^build=(\d{10})\r?$')
        if ($m.Success) { $prior += [UInt64]$m.Groups[1].Value }
      }
    }
  }
  $plan = Get-ReleasePlan $text $RequestedVersion $prior
  $fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,EraseFlash=none'
  [IO.File]::WriteAllText($source,$plan.Text,[Text.UTF8Encoding]::new($false))
  Write-Host "`nRelease source prepared: $($plan.Version) / $($plan.Build)"
  $binPath=$null
  if($Mode -eq 'Auto'){
    $buildDir=Join-Path $Root ('data/ota-build-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($buildDir)|Out-Null
    Write-Host 'Automatic compile / 자동 컴파일: Arduino CLI, ESP32 core 3.3.12'
    Invoke-VisibleCompile $Cli $buildDir (Split-Path $source) $fqbn
    if([IO.File]::ReadAllText($source) -cne $plan.Text){throw 'Source changed during compilation; refusing to package.'}
    $partition=Join-Path $buildDir 'partitions.csv'
    if(!(Test-Path $partition) -or (Get-FileHash $partition).Hash -ne (Get-FileHash (Join-Path (Split-Path $source) 'partitions.csv')).Hash){
      throw 'Build did not preserve the sketch custom partitions.csv.'
    }
    $binPath=Join-Path $buildDir 'gateway.ino.bin'
  }else{
    Write-Host @"

수동 컴파일 및 바이너리 내보내기 / MANUAL COMPILE AND EXPORT
1. Arduino IDE에서 firmware/esp32s3/gateway/gateway.ino를 엽니다.
   Open firmware/esp32s3/gateway/gateway.ino in Arduino IDE.
2. Board: ESP32S3 Dev Module, Flash Size: 16MB, PSRAM: OPI PSRAM,
   USB CDC On Boot: Enabled, USB Mode: Hardware CDC and JTAG,
   Erase All Flash: Disabled를 확인합니다.
3. 스케치의 partitions.csv가 사용되는지 확인한 뒤 Verify/Compile을 실행합니다.
4. 컴파일 성공 후 Sketch > Export Compiled Binary를 실행합니다.
   Wait for compilation to finish, then choose Sketch > Export Compiled Binary.
5. 완료를 확인한 뒤 이 창으로 돌아와 Enter를 누르세요.
   Return here and press Enter only after the export has completed.
"@
    $confirm=Read-Host '컴파일 및 Export Compiled Binary 완료? [Y/N] / Compile and export completed? [Y/N]'
    if($confirm -notmatch '^(?i:y|yes)$'){throw 'Cancelled by user. Source IDs remain set; rerun and choose the same version to resume. No package was created.'}
    $binPath=(Read-Host '내보낸 gateway.ino.bin 경로 입력(파일을 창에 끌어놓아도 됨) / Enter exported BIN path').Trim().Trim('"')
  }
  if([IO.File]::ReadAllText($source) -cne $plan.Text){throw 'Source changed after compile; refusing to package a mismatched BIN.'}
  # This checks package integrity only. The device applies its own current-build/version gate at upload time.
  $packageTestBuild=[UInt64]$plan.Build - 1
  & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File (Join-Path $Root 'tools/create_esp32_ota_package.ps1') `
    -NonInteractive -FirmwareBin $binPath -Version $plan.Version -BuildNumber $plan.Build `
    -InstalledVersion '0.0.0' -InstalledBuild $packageTestBuild -OutputRoot dist
  if ($LASTEXITCODE -ne 0) { throw 'Package validation failed; no release was accepted.' }
  Write-Host 'Use admin and your existing local OTA token. OTA is not sent automatically.'
}

if ($MyInvocation.InvocationName -ne '.') {
  try {
    if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required.' }
    $root = Split-Path -Parent $PSScriptRoot
    $preview = Get-ReleasePlan ([IO.File]::ReadAllText((Join-Path $root 'firmware/esp32s3/gateway/gateway.ino'))) '' @()
    if(!$Version){$Version=Read-Host "Version [$($preview.Version)] / press Enter for automatic patch increment";if(!$Version){$Version=$preview.Version}}
    if(!$Mode){Write-Host "`n빌드 방식을 선택하세요 / Choose build mode`n  1. Auto compile with progress / 자동 컴파일 및 진행 표시`n  2. Manual Arduino IDE compile/export / 직접 컴파일·내보내기"; $choice=Read-Host '선택 / Select [1/2, default 1]';$Mode=if($choice -eq '2'){'Manual'}else{'Auto'}}
    Invoke-Release $root $Version $ArduinoCli $Mode
  } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
}
