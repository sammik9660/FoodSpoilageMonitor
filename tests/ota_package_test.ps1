param(
  [Parameter(Mandatory=$true)][string]$FirmwareBin,
  [string]$Version = '2.0.8',
  [UInt64]$BuildNumber = 2026101906,
  [UInt64]$InstalledBuild = 2026101905
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $FirmwareBin))
$manifest = [IO.File]::ReadAllText([IO.Path]::ChangeExtension((Resolve-Path -LiteralPath $FirmwareBin), '.manifest'))
$match = [regex]::Match($manifest, '\AFSGWOTA1\ntarget=esp32s3\nversion=(?<version>\d+\.\d+\.\d+)\nbuild=(?<build>\d+)\nsha256=(?<sha>[0-9a-f]{64})\nsignature=(?<sig>[A-Za-z0-9+/=]+)\n\z')
if (!$match.Success) { throw 'FORMAT/TARGET: unexpected manifest bytes or fields' }
if ($match.Groups['version'].Value -ne $Version) { throw 'VERSION mismatch' }
if ([UInt64]$match.Groups['build'].Value -ne $BuildNumber -or $BuildNumber -le $InstalledBuild) { throw 'BUILD mismatch or rollback' }
if ($bin.Length -eq 0 -or $bin.Length -gt 0x300000 -or $bin[0] -ne 0xE9) { throw 'Invalid ESP image size/header' }
if (![Text.Encoding]::ASCII.GetString($bin).Contains($Version + [char]0)) { throw 'BIN version missing' }
$sha = (Get-FileHash -LiteralPath $FirmwareBin -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sha -ne $match.Groups['sha'].Value) { throw 'BIN SHA-256 mismatch' }
$canonical = "FSGWOTA1`nesp32s3`n$Version`n$BuildNumber`n$sha`n"
$hash = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($canonical))
$header = Get-Content (Join-Path $root 'firmware/esp32s3/gateway/ota_signing_key.h') -Raw
$parts = [regex]::Matches($header, '"([^"\\]*(?:\\.[^"\\]*)*)"') | ForEach-Object { $_.Groups[1].Value.Replace('\n', "`n") }
$key = [Security.Cryptography.ECDsa]::Create()
try {
  $key.ImportFromPem(($parts -join ''))
  $signature = [Convert]::FromBase64String($match.Groups['sig'].Value)
  $format = [Security.Cryptography.DSASignatureFormat]::Rfc3279DerSequence
  if (!$key.VerifyHash($hash, $signature, $format)) { throw 'SIGNATURE invalid' }
  $hash[0] = $hash[0] -bxor 1
  if ($key.VerifyHash($hash, $signature, $format)) { throw 'Tampered canonical hash accepted' }
} finally { $key.Dispose() }
"PASS package format, version, build, size, SHA-256, embedded-key signature and tamper rejection; bytes=$($bin.Length) sha256=$sha"
