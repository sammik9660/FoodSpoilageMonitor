param(
  [Parameter(Mandatory=$true)][string]$PackageZip,
  [Parameter(Mandatory=$true)][string]$Version
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or newer is required.' }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected firmware version must be major.minor.patch.' }
if (!(Test-Path -LiteralPath $PackageZip -PathType Leaf)) { throw "DFU ZIP not found: $PackageZip" }

function Get-ZipBytes($entry) {
  $stream = $entry.Open()
  $memory = [IO.MemoryStream]::new()
  try { $stream.CopyTo($memory); return $memory.ToArray() }
  finally { $stream.Dispose(); $memory.Dispose() }
}
function Get-Crc32([byte[]]$bytes) {
  $table = New-Object 'uint32[]' 256
  for ($i=0; $i -lt 256; $i++) {
    [uint32]$value = $i
    for ($bit=0; $bit -lt 8; $bit++) {
      if ($value -band 1) { $value = [uint32](($value -shr 1) -bxor 0xEDB88320L) }
      else { $value = [uint32]($value -shr 1) }
    }
    $table[$i] = $value
  }
  [uint32]$crc = 0xFFFFFFFFL
  foreach ($value in $bytes) {
    $index = [int](($crc -bxor [uint32]$value) -band 0xFF)
    $crc = [uint32](($crc -shr 8) -bxor $table[$index])
  }
  return [uint32]($crc -bxor 0xFFFFFFFFL)
}
function Get-Crc16([byte[]]$bytes) {
  $table = New-Object 'int[]' 256
  for ($i=0; $i -lt 256; $i++) {
    $value = $i -shl 8
    for ($bit=0; $bit -lt 8; $bit++) {
      if ($value -band 0x8000) { $value = (($value -shl 1) -bxor 0x1021) -band 0xFFFF }
      else { $value = ($value -shl 1) -band 0xFFFF }
    }
    $table[$i] = $value
  }
  $crc = 0xFFFF
  foreach ($value in $bytes) {
    $index = (($crc -shr 8) -bxor [int]$value) -band 0xFF
    $crc = (($crc -shl 8) -bxor $table[$index]) -band 0xFFFF
  }
  return $crc
}

$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $PackageZip))
try {
  $entries = @($zip.Entries)
  if ($entries.Count -ne 3 -or @($entries | Where-Object { $_.FullName -match '[/\\]' }).Count) {
    throw 'DFU ZIP must contain exactly three root files: manifest, application BIN and DAT.'
  }
  $entryMap = @{}
  foreach ($entry in $entries) {
    if ($entryMap.ContainsKey($entry.FullName)) { throw 'Duplicate ZIP entry.' }
    $entryMap[$entry.FullName] = $entry
    $bytes = Get-ZipBytes $entry
    if ((Get-Crc32 $bytes) -ne $entry.Crc32) { throw "ZIP CRC32 mismatch: $($entry.FullName)" }
  }
  if (!$entryMap.ContainsKey('manifest.json')) { throw 'DFU manifest.json is missing.' }
  $manifest = [Text.Encoding]::UTF8.GetString((Get-ZipBytes $entryMap['manifest.json'])) | ConvertFrom-Json -AsHashtable
  if (@($manifest.Keys).Count -ne 1 -or !$manifest.ContainsKey('manifest')) { throw 'Unexpected DFU manifest root.' }
  $body = $manifest.manifest
  if (@($body.Keys).Count -ne 2 -or !$body.ContainsKey('application') -or [double]$body.dfu_version -ne 0.5) {
    throw 'DFU ZIP is not application-only Legacy DFU 0.5.'
  }
  $app = $body.application
  if (@($app.Keys).Count -ne 3 -or !$app.ContainsKey('bin_file') -or !$app.ContainsKey('dat_file') -or
      !$app.ContainsKey('init_packet_data')) { throw 'Unexpected DFU application metadata.' }
  $binName = [string]$app.bin_file; $datName = [string]$app.dat_file
  if ($binName -notmatch '^[A-Za-z0-9_.-]+\.bin$' -or $datName -notmatch '^[A-Za-z0-9_.-]+\.dat$' -or
      !$entryMap.ContainsKey($binName) -or !$entryMap.ContainsKey($datName)) {
    throw 'DFU application BIN or DAT is missing or has an unsafe path.'
  }
  $packet = $app.init_packet_data
  if ([int]$packet.device_type -ne 0x0052 -or @($packet.softdevice_req).Count -ne 1 -or
      [int]$packet.softdevice_req[0] -ne 0x00B6) {
    throw 'DFU device type or S140 6.1.1 SoftDevice requirement mismatch.'
  }
  $bin = Get-ZipBytes $entryMap[$binName]
  $dat = Get-ZipBytes $entryMap[$datName]
  if (!$bin.Length -or $bin.Length -gt 815104) { throw 'DFU application BIN is empty or exceeds the board application limit.' }
  if ($dat.Length -ne 14 -or [BitConverter]::ToUInt16($dat,0) -ne 0x0052 -or
      [BitConverter]::ToUInt16($dat,8) -ne 1 -or [BitConverter]::ToUInt16($dat,10) -ne 0x00B6) {
    throw 'Legacy DFU DAT device type, format or SoftDevice requirement mismatch.'
  }
  $crc16 = Get-Crc16 $bin
  if ($crc16 -ne [int]$packet.firmware_crc16 -or $crc16 -ne [BitConverter]::ToUInt16($dat,12)) {
    throw 'Application BIN CRC16 does not match manifest and DAT.'
  }
  if (![Text.Encoding]::Latin1.GetString($bin).Contains($Version + [char]0)) {
    throw 'The application BIN does not contain the expected source firmware version.'
  }
  Write-Output "PASS application-only Legacy DFU 0.5 version=$Version device=0x0052 sd_req=0x00B6 bytes=$($bin.Length) crc16=$crc16"
} finally { $zip.Dispose() }
