$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ('fsm-ota-sign-test-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null
try {
  $bin = Join-Path $temp 'gateway.bin'
  [System.IO.File]::WriteAllBytes($bin, [byte[]](0..255))
  & (Join-Path $root 'tools/sign_esp32_ota.ps1') -FirmwareBin $bin -Version '2.0.1' -BuildNumber 2026101702 -PrivateKey (Join-Path $root 'secrets/esp32-ota-signing-key.pem') | Out-Null
  $manifest = [System.IO.File]::ReadAllText([System.IO.Path]::ChangeExtension($bin, '.manifest'))
  $fields = @{}
  foreach ($line in ($manifest -split "`n")) {
    if ($line -match '^([^=]+)=(.*)$') { $fields[$Matches[1]] = $Matches[2].TrimEnd("`r") }
  }
  $digest = (Get-FileHash -Algorithm SHA256 -LiteralPath $bin).Hash.ToLowerInvariant()
  if ($manifest -notmatch '^FSGWOTA1\ntarget=esp32s3\n') { throw 'Invalid manifest header.' }
  if ($fields['version'] -ne '2.0.1' -or $fields['build'] -ne '2026101702' -or $fields['sha256'] -ne $digest) { throw 'Manifest metadata mismatch.' }
  $canonical = "FSGWOTA1`nesp32s3`n$($fields['version'])`n$($fields['build'])`n$digest`n"
  $hash = [System.Security.Cryptography.SHA256]::HashData([System.Text.Encoding]::UTF8.GetBytes($canonical))
  $header = Get-Content (Join-Path $root 'firmware/esp32s3/gateway/ota_signing_key.h') -Raw
  $pemLines = [regex]::Matches($header, '"([^"\\]*(?:\\.[^"\\]*)*)"') | ForEach-Object { $_.Groups[1].Value }
  $pem = ($pemLines | ForEach-Object { $_ -replace '\\n', "`n" }) -join ''
  $verifier = [System.Security.Cryptography.ECDsa]::Create()
  $verifier.ImportFromPem($pem)
  $signature = [Convert]::FromBase64String($fields['signature'])
  if (-not $verifier.VerifyHash($hash, $signature, [System.Security.Cryptography.DSASignatureFormat]::Rfc3279DerSequence)) { throw 'Public-key signature verification failed.' }
  [System.IO.File]::WriteAllBytes($bin, [byte[]](1..255))
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $bin).Hash.ToLowerInvariant() -eq $digest) { throw 'Changed image matched original digest.' }
  'PASS OTA manifest signature and image-integrity test'
}
finally {
  Remove-Item -LiteralPath $temp -Recurse -Force
}
