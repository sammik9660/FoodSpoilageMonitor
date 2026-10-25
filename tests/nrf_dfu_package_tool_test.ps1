$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$known = Join-Path $root 'data/nrf-dfu-2.0.6/bme688-node-2.0.6-legacy-dfu.zip'
$verify = Join-Path $root 'tools/verify_nrf_legacy_dfu.ps1'
if (!(Test-Path -LiteralPath $known -PathType Leaf)) { throw 'Known successful Legacy DFU reference ZIP is missing.' }
$result = & pwsh -NoProfile -File $verify -PackageZip $known -Version '2.0.6' 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or !$result.Contains('PASS application-only Legacy DFU 0.5')) {
  throw 'Known successful package was rejected.'
}
$wrongVersion = & pwsh -NoProfile -File $verify -PackageZip $known -Version '2.0.11' 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -or !$wrongVersion.Contains('expected source firmware version')) {
  throw 'Version mismatch was not rejected.'
}
$tempRoot = Join-Path $root ('data/nrf-zip-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($tempRoot) | Out-Null
try {
  $extra = Join-Path $tempRoot 'with-bootloader.zip'
  [IO.File]::Copy($known, $extra)
  $zip = [IO.Compression.ZipFile]::Open($extra, [IO.Compression.ZipArchiveMode]::Update)
  try {
    $entry = $zip.CreateEntry('bootloader.bin')
    $stream = $entry.Open()
    try { $stream.WriteByte(1) } finally { $stream.Dispose() }
  } finally { $zip.Dispose() }
  $rejected = & pwsh -NoProfile -File $verify -PackageZip $extra -Version '2.0.6' 2>&1 | Out-String
  if ($LASTEXITCODE -eq 0 -or !$rejected.Contains('exactly three root files')) {
    throw 'ZIP carrying a bootloader entry was not rejected.'
  }
  'PASS known Legacy DFU ZIP; reject version mismatch and bootloader entry'
} finally {
  $target = [IO.Path]::GetFullPath($tempRoot)
  $base = [IO.Path]::GetFullPath((Join-Path $root 'data'))
  if (!$target.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test directory escaped data; manual cleanup required.'
  }
  Remove-Item -LiteralPath $target -Recurse -Force
}
