param([string]$OutputDirectory = "secrets")
$ErrorActionPreference = 'Stop'
$privatePath = Join-Path $OutputDirectory 'esp32-ota-signing-key.pem'
if (Test-Path -LiteralPath $privatePath) { throw "Refusing to overwrite existing signing key: $privatePath" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$curve = [System.Security.Cryptography.ECCurve]::CreateFromValue('1.2.840.10045.3.1.7')
$key = [System.Security.Cryptography.ECDsa]::Create()
$key.GenerateKey($curve)
[System.IO.File]::WriteAllText($privatePath, $key.ExportPkcs8PrivateKeyPem())
$publicPem = $key.ExportSubjectPublicKeyInfoPem()
Write-Output "Private signing key created at $privatePath. Back it up securely; never commit it."
Write-Output 'Update ota_signing_key.h with this public key before building firmware:'
Write-Output $publicPem
