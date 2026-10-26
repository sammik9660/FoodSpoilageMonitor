param(
  [string]$FirmwareBin,
  [string]$Version,
  [string]$BuildNumber,
  [string]$InstalledVersion,
  [string]$InstalledBuild,
  [string]$PrivateKeyPath = 'secrets/esp32-ota-signing-key.pem',
  [string]$OutputRoot = 'data',
  [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
try {
  if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or newer is required.' }
  $source = [IO.Path]::Combine($root, 'firmware/esp32s3/gateway/gateway.ino')
  $signer = [IO.Path]::Combine($root, 'tools/sign_esp32_ota.ps1')
  $tester = [IO.Path]::Combine($root, 'tests/ota_package_test.ps1')
  foreach ($file in @($source, $signer, $tester)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required project file is missing: $file" }
  }
  $keyPath = [IO.Path]::GetFullPath([IO.Path]::Combine($root, $PrivateKeyPath))
  if (!(Test-Path -LiteralPath $keyPath -PathType Leaf)) {
    throw 'Signing key is missing. Restore secrets/esp32-ota-signing-key.pem from the existing local backup; no key was generated.'
  }

  $text = [IO.File]::ReadAllText($source)
  $sourceVersion = [regex]::Match($text, '(?m)^#define FSGW_VERSION "(?<value>\d+\.\d+\.\d+)"\s*$')
  $sourceBuild = [regex]::Match($text, '(?m)^#define FSGW_BUILD (?<value>\d{10})\s*$')
  if (!$sourceVersion.Success -or !$sourceBuild.Success) { throw 'Source release identifiers are missing or ambiguous.' }
  $expectedVersion = $sourceVersion.Groups['value'].Value
  $expectedBuild = $sourceBuild.Groups['value'].Value
  Write-Output "Source version=$expectedVersion build=$expectedBuild"
  if (!$NonInteractive) {
    if (!$FirmwareBin) { $FirmwareBin = Read-Host 'Enter the exported application gateway.ino.bin path (drag and drop is supported)' }
    if (!$Version) { $Version = Read-Host "Confirm version [$expectedVersion] (Enter keeps source value)" }
    if (!$BuildNumber) { $BuildNumber = Read-Host "Confirm build [$expectedBuild] (Enter keeps source value)" }
    if (!$InstalledVersion) { $InstalledVersion = Read-Host 'Enter the version currently running on the ESP32 (OTA? result)' }
    if (!$InstalledBuild) { $InstalledBuild = Read-Host 'Enter the build currently running on the ESP32 (OTA? result)' }
  }
  if (!$Version) { $Version = $expectedVersion }
  if (!$BuildNumber) { $BuildNumber = $expectedBuild }
  if ($Version -ne $expectedVersion -or $BuildNumber -ne $expectedBuild) {
    throw 'Entered release identifiers differ from gateway.ino. Edit the source and export a new compiled BIN before signing.'
  }
  if ($InstalledVersion -notmatch '^\d+\.\d+\.\d+$' -or $InstalledBuild -notmatch '^\d{10}$') {
    throw 'Enter the installed version and ten-digit build shown by OTA?; do not guess.'
  }
  if ($Version -eq $InstalledVersion -or [UInt64]$BuildNumber -le [UInt64]$InstalledBuild) {
    throw 'OTA anti-rollback check failed: version must change and build must exceed the installed build.'
  }
  if (!$FirmwareBin) { throw 'Application BIN path is required.' }
  $FirmwareBin = $FirmwareBin.Trim().Trim('"')
  $binPath = [IO.Path]::GetFullPath([IO.Path]::Combine((Get-Location).Path, $FirmwareBin))
  if (!(Test-Path -LiteralPath $binPath -PathType Leaf)) { throw "Application BIN not found: $binPath" }
  if ([IO.Path]::GetFileName($binPath) -ne 'gateway.ino.bin') {
    throw 'Select the application-only gateway.ino.bin, not a merged image, bootloader, or partition binary.'
  }
  $bin = [IO.File]::ReadAllBytes($binPath)
  if ($bin.Length -eq 0 -or $bin.Length -gt 0x300000 -or $bin[0] -ne 0xE9) {
    throw 'Invalid ESP application image header or size (3 MiB OTA slot maximum).'
  }
  $identities = [regex]::Matches([Text.Encoding]::Latin1.GetString($bin), 'FSGWBUILD:(?<version>\d+\.\d+\.\d+):(?<build>\d{10})\x00')
  if ($identities.Count -ne 1) { throw 'BIN release identity is absent or ambiguous. Export a new BIN from the current gateway.ino.' }
  $identity = $identities[0]
  if ($identity.Groups['version'].Value -ne $Version -or $identity.Groups['build'].Value -ne $BuildNumber) {
    throw 'BIN version/build differs from the source. Recompile and export the application BIN; signing does not change it.'
  }
  $outputPath = [IO.Path]::GetFullPath([IO.Path]::Combine($root, $OutputRoot))
  if (!$outputPath.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output root must remain inside the project.'
  }
  [IO.Directory]::CreateDirectory($outputPath) | Out-Null
  foreach ($folder in @(Get-ChildItem -LiteralPath $outputPath -Directory -Filter 'ota-*')) {
    $priorManifest = [IO.Path]::Combine($folder.FullName, 'gateway.ino.manifest')
    if (!(Test-Path -LiteralPath $priorManifest -PathType Leaf)) { continue }
    $priorBuild = [regex]::Match([IO.File]::ReadAllText($priorManifest), '(?m)^build=(?<value>\d{10})$')
    if ($priorBuild.Success -and [UInt64]$BuildNumber -le [UInt64]$priorBuild.Groups['value'].Value) {
      throw "Build must exceed existing package build $($priorBuild.Groups['value'].Value); no existing package was changed."
    }
  }
  $releaseName = "ota-$Version-$BuildNumber"
  $finalPath = [IO.Path]::Combine($outputPath, $releaseName)
  if (Test-Path -LiteralPath $finalPath) { throw "Release folder already exists; it will not be overwritten: $finalPath" }
  $stagePath = [IO.Path]::Combine($outputPath, ".ota-stage-$([Guid]::NewGuid().ToString('N'))")
  [IO.Directory]::CreateDirectory($stagePath) | Out-Null
  try {
    $stageBin = [IO.Path]::Combine($stagePath, 'gateway.ino.bin')
    [IO.File]::Copy($binPath, $stageBin)
    & $signer -FirmwareBin $stageBin -Version $Version -BuildNumber ([UInt64]$BuildNumber) -PrivateKey $keyPath | Out-Null
    & $tester -FirmwareBin $stageBin -Version $Version -BuildNumber ([UInt64]$BuildNumber) -InstalledBuild ([UInt64]$InstalledBuild) | Write-Output
    $hashLines = foreach ($artifact in Get-ChildItem -LiteralPath $stagePath -File) {
      "$((Get-FileHash -LiteralPath $artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($artifact.Name)"
    }
    [IO.File]::WriteAllLines((Join-Path $stagePath 'SHA256SUMS.txt'), [string[]]$hashLines)
    [IO.File]::WriteAllText((Join-Path $stagePath 'RELEASE.txt'), "target=esp32s3`nversion=$Version`nbuild=$BuildNumber`nboard=ESP32-S3 N16R8`nflash=16MB`npsram=OPI`nota_slot_bytes=3145728`nUpload gateway.ino.manifest as Manifest and gateway.ino.bin as Firmware.`nDevice installation and hardware validation have NOT been performed.`n")
    if (Test-Path -LiteralPath $finalPath) { throw 'Release folder appeared during verification; refusing to overwrite it.' }
    [IO.Directory]::Move($stagePath, $finalPath)
  } finally {
    if (Test-Path -LiteralPath $stagePath) {
      if (!$stagePath.StartsWith($outputPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary path escaped the output root; manual cleanup required.'
      }
      Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
  }
  $sha = (Get-FileHash -LiteralPath ([IO.Path]::Combine($finalPath, 'gateway.ino.bin')) -Algorithm SHA256).Hash.ToLowerInvariant()
  Write-Output "Verified BIN: $([IO.Path]::Combine($finalPath, 'gateway.ino.bin'))"
  Write-Output "Verified manifest: $([IO.Path]::Combine($finalPath, 'gateway.ino.manifest'))"
  Write-Output "BIN SHA-256: $sha"
  Write-Output "Open http://<gateway-ip>:8080/ on the same LAN. Select .manifest under Manifest and .bin under Firmware."
} catch {
  [Console]::Error.WriteLine("OTA package error: $($_.Exception.Message)")
  exit 1
}
