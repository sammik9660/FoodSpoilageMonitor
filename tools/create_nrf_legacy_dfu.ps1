param(
  [string]$OutputRoot = 'data',
  [string]$ArduinoCli
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage = $null
$releaseStage = $null
try {
  if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or newer is required.' }
  $sketch = [IO.Path]::Combine($root, 'firmware/nrf52840/bme688_node')
  $source = [IO.Path]::Combine($sketch, 'bme688_node.ino')
  $verifier = [IO.Path]::Combine($root, 'tools/verify_nrf_legacy_dfu.ps1')
  if (!(Test-Path -LiteralPath $source -PathType Leaf) -or !(Test-Path -LiteralPath $verifier -PathType Leaf)) {
    throw 'nRF sketch or Legacy DFU verifier is missing.'
  }
  $sourceText = [IO.File]::ReadAllText($source)
  $match = [regex]::Match($sourceText, 'constexpr\s+char\s+SENSOR_FIRMWARE_VERSION\[\]\s*=\s*"(?<version>\d+\.\d+\.\d+)"\s*;')
  if (!$match.Success) { throw 'Sensor firmware version cannot be read from bme688_node.ino.' }
  $version = $match.Groups['version'].Value
  $fqbn = 'adafruit:nrf52:feather52840:softdevice=s140v6'

  if (!$ArduinoCli) {
    $found = Get-Command arduino-cli.exe -ErrorAction SilentlyContinue
    if ($found) { $ArduinoCli = $found.Source }
    else {
      $bundled = [IO.Path]::Combine($env:LOCALAPPDATA, 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe')
      if (Test-Path -LiteralPath $bundled -PathType Leaf) { $ArduinoCli = $bundled }
    }
  }
  if (!$ArduinoCli -or !(Test-Path -LiteralPath $ArduinoCli -PathType Leaf)) {
    throw 'arduino-cli.exe is missing. Install Arduino CLI or Arduino IDE 2 and retry.'
  }
  $properties = (& $ArduinoCli compile --fqbn $fqbn --show-properties $sketch) -join "`n"
  if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI could not read the selected nRF52 board properties.' }
  $platform = [regex]::Match($properties, '(?m)^runtime\.platform\.path=(?<path>[^\r\n]+)')
  if (!$platform.Success -or $platform.Groups['path'].Value -notmatch '[/\\]adafruit[/\\]hardware[/\\]nrf52[/\\]1\.7\.0$' -or
      $properties -notmatch '(?m)^build\.sd_name=s140$' -or
      $properties -notmatch '(?m)^build\.sd_version=6\.1\.1$' -or
      $properties -notmatch '(?m)^build\.sd_fwid=0x00B6$' -or
      $properties -notmatch '(?m)^build\.ldscript=nrf52840_s140_v6\.ld$' -or
      $properties -notmatch '(?m)^recipe\.objcopy\.zip\.pattern=.*dfu genpkg --dev-type 0x0052 --sd-req 0x00B6 --application ') {
    throw 'Selected board/core is not Adafruit nRF52 1.7.0 Feather nRF52840 with S140 6.1.1 application-only Legacy DFU.'
  }
  Write-Output "Core 1.7.0 and S140 6.1.1 verified; source version=$version"

  $dataRoot = [IO.Path]::GetFullPath([IO.Path]::Combine($root, $OutputRoot))
  if (!$dataRoot.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output root must remain inside the project.'
  }
  [IO.Directory]::CreateDirectory($dataRoot) | Out-Null
  $release = [IO.Path]::Combine($dataRoot, "nrf-dfu-$version")
  if (Test-Path -LiteralPath $release) {
    throw "Release folder already exists and will not be overwritten: $release. Increase SENSOR_FIRMWARE_VERSION and rebuild for a new release."
  }
  $stage = [IO.Path]::Combine($dataRoot, ".nrf-dfu-build-$([Guid]::NewGuid().ToString('N'))")
  [IO.Directory]::CreateDirectory($stage) | Out-Null
  $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  Write-Output "Compiling $fqbn; Adafruit core generates the Legacy DFU ZIP."
  & $ArduinoCli compile --fqbn $fqbn --output-dir $stage $sketch
  if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI compile or Adafruit DFU package generation failed.' }
  if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'Sensor source changed during compilation; generated ZIP was not released.'
  }
  $generated = [IO.Path]::Combine($stage, 'bme688_node.ino.zip')
  if (!(Test-Path -LiteralPath $generated -PathType Leaf)) {
    throw 'Adafruit core did not generate bme688_node.ino.zip; do not package files manually.'
  }
  & $verifier -PackageZip $generated -Version $version | Write-Output
  $releaseStage = [IO.Path]::Combine($dataRoot, ".nrf-dfu-release-$([Guid]::NewGuid().ToString('N'))")
  [IO.Directory]::CreateDirectory($releaseStage) | Out-Null
  $filename = "bme688-node-$version-legacy-dfu.zip"
  $stagedZip = [IO.Path]::Combine($releaseStage, $filename)
  [IO.File]::Copy($generated, $stagedZip)
  $sha = (Get-FileHash -LiteralPath $stagedZip -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText([IO.Path]::Combine($releaseStage, 'SHA256SUMS.txt'), "$sha  $filename`n", [Text.UTF8Encoding]::new($false))
  if (Test-Path -LiteralPath $release) { throw 'Release folder appeared during build; refusing to overwrite it.' }
  [IO.Directory]::Move($releaseStage, $release)
  Write-Output "Verified ZIP: $([IO.Path]::Combine($release, $filename))"
  Write-Output "SHA256SUMS: $([IO.Path]::Combine($release, 'SHA256SUMS.txt'))"
  Write-Output "SHA-256: $sha"
  Write-Output 'ESP32 relay: on the same LAN open http://<gateway-ip>:8080/nrf-dfu, upload ZIP, then approve Start update.'
  Write-Output 'Phone: disconnect the gateway sensor link, open Nordic nRF Device Firmware Update, disable Request high MTU, use PRN 8 or lower, and select this ZIP.'
} catch {
  [Console]::Error.WriteLine("nRF DFU package error: $($_.Exception.Message)")
  exit 1
} finally {
  foreach ($temp in @($stage, $releaseStage)) {
    if (!$temp -or !(Test-Path -LiteralPath $temp)) { continue }
    $resolved = [IO.Path]::GetFullPath($temp)
    $base = [IO.Path]::GetFullPath([IO.Path]::Combine($root, $OutputRoot))
    if (!$resolved.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
      [Console]::Error.WriteLine('Temporary path escaped output root; manual cleanup required.')
      continue
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
  }
}
