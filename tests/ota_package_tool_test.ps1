$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = [IO.File]::ReadAllText((Join-Path $root 'firmware/esp32s3/gateway/gateway.ino'))
$version = [regex]::Match($source, '(?m)^#define FSGW_VERSION "([^\"]+)"').Groups[1].Value
$build = [regex]::Match($source, '(?m)^#define FSGW_BUILD (\d{10})').Groups[1].Value
if (!$version -or !$build) { throw 'Release identifiers missing from source.' }
$previousBuild = [string]([UInt64]$build - 1)
$testRoot = Join-Path $root ('data/ota-tool-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$bin = Join-Path $testRoot 'gateway.ino.bin'
$tool = Join-Path $root 'tools/create_esp32_ota_package.ps1'
function Expect-Rejection([string]$reason, [string]$runningBuild, [string[]]$extraArgs) {
  $output = & pwsh -NoProfile -File $tool -NonInteractive -FirmwareBin $bin -Version $version `
    -BuildNumber $build -InstalledVersion '0.0.0' -InstalledBuild $runningBuild `
    -OutputRoot $testRoot @extraArgs 2>&1 | Out-String
  if ($LASTEXITCODE -ne 1 -or !$output.Contains($reason)) { throw "Expected package rejection: $reason" }
}
try {
  [IO.File]::WriteAllBytes($bin, [byte[]](0,1,2))
  Expect-Rejection 'Signing key is missing' $previousBuild @('-PrivateKeyPath','secrets/missing-test-key.pem')
  Expect-Rejection 'Invalid ESP application image' $previousBuild @()
  Expect-Rejection 'anti-rollback check failed' $build @()

  $old = New-Object byte[] 128
  $old[0] = 0xE9
  [IO.File]::WriteAllBytes($bin, $old)
  Expect-Rejection 'BIN release identity is absent' $previousBuild @()

  $valid = New-Object byte[] 128
  $valid[0] = 0xE9
  $wrongMarker = [Text.Encoding]::ASCII.GetBytes("FSGWBUILD:0.0.0:${build}`0")
  [Array]::Copy($wrongMarker, 0, $valid, 1, $wrongMarker.Length)
  [IO.File]::WriteAllBytes($bin, $valid)
  Expect-Rejection 'BIN version/build differs' $previousBuild @()
  $valid = New-Object byte[] 128
  $valid[0] = 0xE9
  $marker = [Text.Encoding]::ASCII.GetBytes("FSGWBUILD:${version}:${build}`0")
  [Array]::Copy($marker, 0, $valid, 1, $marker.Length)
  [IO.File]::WriteAllBytes($bin, $valid)
  $prior = Join-Path $testRoot 'ota-prior'
  [IO.Directory]::CreateDirectory($prior) | Out-Null
  [IO.File]::WriteAllText((Join-Path $prior 'gateway.ino.manifest'), "build=$build`n")
  Expect-Rejection 'Build must exceed existing package build' $previousBuild @()
  'PASS OTA package tool rejects missing key, invalid/old BIN, rollback and duplicate build'
} finally {
  $resolved = [IO.Path]::GetFullPath($testRoot)
  $dataRoot = [IO.Path]::GetFullPath((Join-Path $root 'data'))
  if (!$resolved.StartsWith($dataRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test directory escaped data; manual cleanup required.'
  }
  Remove-Item -LiteralPath $resolved -Recurse -Force
}
