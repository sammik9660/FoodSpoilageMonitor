param(
  [Parameter(Mandatory=$true)][string]$FirmwareBin,
  [Parameter(Mandatory=$true)][string]$Version,
  [Parameter(Mandatory=$true)][UInt64]$BuildNumber,
  [string]$PrivateKey = 'secrets/esp32-ota-signing-key.pem'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be major.minor.patch.' }
if (!(Test-Path -LiteralPath $FirmwareBin)) { throw "Firmware not found: $FirmwareBin" }
if (!(Test-Path -LiteralPath $PrivateKey)) { throw "Signing key not found: $PrivateKey" }
if ((Get-Item -LiteralPath $FirmwareBin).Length -gt 0x300000) { throw 'Firmware exceeds the 3 MiB OTA slot.' }
$key = [System.Security.Cryptography.ECDsa]::Create()
$key.ImportFromPem([System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $PrivateKey)))
$digest = (Get-FileHash -Algorithm SHA256 -LiteralPath $FirmwareBin).Hash.ToLowerInvariant()
$canonical = "FSGWOTA1`nesp32s3`n$Version`n$BuildNumber`n$digest`n"
$hashBytes = [System.Security.Cryptography.SHA256]::HashData([System.Text.Encoding]::UTF8.GetBytes($canonical))
$signature = $key.SignHash($hashBytes, [System.Security.Cryptography.DSASignatureFormat]::Rfc3279DerSequence)
$manifest = "FSGWOTA1`ntarget=esp32s3`nversion=$Version`nbuild=$BuildNumber`nsha256=$digest`nsignature=$([Convert]::ToBase64String($signature))`n"
$manifestPath = [System.IO.Path]::ChangeExtension((Resolve-Path -LiteralPath $FirmwareBin), '.manifest')
[System.IO.File]::WriteAllText($manifestPath, $manifest)
Write-Output "Signed manifest: $manifestPath"
