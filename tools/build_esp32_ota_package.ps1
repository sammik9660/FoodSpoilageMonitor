param([string]$Version, [string]$ArduinoCli, [switch]$NonInteractive)
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
  if ($RequestedVersion -notmatch '^\d+\.\d+\.\d+$' -or [version]$RequestedVersion -le [version]$oldVersion) {
    throw 'Choose a major.minor.patch version greater than the current source version.'
  }
  $maximum = $oldBuild
  foreach ($prior in $PriorBuilds) { if ($prior -gt $maximum) { $maximum = $prior } }
  $next = [Math]::Max([UInt64]($Today.ToString('yyyyMMdd') + '01'), $maximum + 1)
  # Keep a valid YYYYMMDDNN even when NN=99 is exhausted or local clock moved backwards.
  $digits = ([UInt64]$next).ToString()
  if ($digits.EndsWith('00')) {
    $date = [datetime]::ParseExact($maximum.ToString().Substring(0,8), 'yyyyMMdd', $null)
    $next = [UInt64]($date.AddDays(1).ToString('yyyyMMdd') + '01')
  }
  if ($next.ToString().Length -ne 10) { throw 'Build number exceeds YYYYMMDDNN.' }
  $changed = $Text.Replace($versions[0].Value, $versions[0].Value.Replace($oldVersion,$RequestedVersion))
  $changed = $changed.Replace($builds[0].Value, $builds[0].Value.Replace($oldBuild.ToString(),$next.ToString()))
  [pscustomobject]@{ Version=$RequestedVersion; Build=$next.ToString(); OldVersion=$oldVersion; OldBuild=$oldBuild; Text=$changed }
}

function Invoke-AutomatedRelease([string]$Root, [string]$RequestedVersion, [string]$Cli) {
  $source = Join-Path $Root 'firmware/esp32s3/gateway/gateway.ino'
  $key = Join-Path $Root 'secrets/esp32-ota-signing-key.pem'
  if (!(Test-Path -LiteralPath $key)) { throw 'Existing signing key missing; no source was changed.' }
  if (!$Cli) {
    $command = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($command) { $Cli = $command.Source }
    else { $Cli = Join-Path $env:LOCALAPPDATA 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' }
  }
  if (!(Test-Path -LiteralPath $Cli)) { throw 'arduino-cli not found; install Arduino CLI or Arduino IDE first.' }
  $cores = & $Cli core list --format json | ConvertFrom-Json
  if ($LASTEXITCODE -ne 0 -or !(@($cores.platforms) | Where-Object { $_.id -eq 'esp32:esp32' -and $_.installed_version -eq '3.3.12' })) {
    throw 'ESP32 Arduino core 3.3.12 is required; no automatic core changes are performed.'
  }
  $original = [IO.File]::ReadAllBytes($source)
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
  $buildDir = Join-Path $Root ('data/ota-build-' + [guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($buildDir) | Out-Null
  $success = $false
  try {
    [IO.File]::WriteAllText($source,$plan.Text,[Text.UTF8Encoding]::new($false))
    Write-Host "Building $($plan.Version) / $($plan.Build). Please wait; no device upload."
    & $Cli compile --fqbn $fqbn --build-path $buildDir (Split-Path $source) *> (Join-Path $buildDir 'compile.log')
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed. Local log: $buildDir/compile.log (do not share without removing secrets)." }
    if ([IO.File]::ReadAllText($source) -cne $plan.Text) { throw 'Source changed during compilation; refusing to package.' }
    $partition = Join-Path $buildDir 'partitions.csv'
    if (!(Test-Path $partition) -or (Get-FileHash $partition).Hash -ne (Get-FileHash (Join-Path (Split-Path $source) 'partitions.csv')).Hash) {
      throw 'Build did not preserve the sketch custom partitions.csv.'
    }
    & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File (Join-Path $Root 'tools/create_esp32_ota_package.ps1') `
      -NonInteractive -FirmwareBin (Join-Path $buildDir 'gateway.ino.bin') -Version $plan.Version -BuildNumber $plan.Build `
      -InstalledVersion $plan.OldVersion -InstalledBuild $plan.OldBuild -OutputRoot dist
    if ($LASTEXITCODE -ne 0) { throw 'Package validation failed; no release was accepted.' }
    $success = $true
    Write-Host 'Use admin and your existing local OTA token. The device still enforces its own anti-rollback and safety checks.'
  } finally {
    if (!$success -and [IO.File]::ReadAllText($source) -ceq $plan.Text) {
      [IO.File]::WriteAllBytes($source,$original)
      Write-Host 'Restored the exact pre-run source; existing user edits preserved.'
    }
  }
}

if ($MyInvocation.InvocationName -ne '.') {
  try {
    if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required.' }
    $root = Split-Path -Parent $PSScriptRoot
    if (!$NonInteractive -and !$Version) {
      $preview = Get-ReleasePlan ([IO.File]::ReadAllText((Join-Path $root 'firmware/esp32s3/gateway/gateway.ino'))) '' @()
      $Version = Read-Host "Version [$($preview.Version)] - press Enter for automatic patch increment"
    }
    Invoke-AutomatedRelease $root $Version $ArduinoCli
  } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
}
