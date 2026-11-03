param(
  [Parameter(Mandatory=$true)][ValidateSet('esp32','nrf')][string]$Kind,
  [Parameter(Mandatory=$true)][string]$PackagePath,
  [Parameter(Mandatory=$true)][string]$ReleaseId,
  [Parameter(Mandatory=$true)][string]$BaseUrl,
  [string]$Version,
  [switch]$AllowLegacyTarget,
  [string]$OutputFile
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 is required.'}
if($ReleaseId -notmatch '^[a-zA-Z0-9_-]{1,48}$'){throw 'Invalid release ID.'}
$root=Split-Path $PSScriptRoot -Parent
$BaseUrl=$BaseUrl.TrimEnd('/')
if($BaseUrl -notmatch '^https://[a-z0-9.-]+(?:/[A-Za-z0-9/_-]+)?$' -or $BaseUrl.Contains('..')){throw 'Use an immutable HTTPS directory URL without redirects, credentials or query strings.'}
$entry=[ordered]@{id=$ReleaseId;kind=$Kind}
if($Kind -eq 'esp32'){
  $bin=Join-Path $PackagePath 'gateway.ino.bin';$manifest=Join-Path $PackagePath 'gateway.ino.manifest'
  $text=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $manifest))
  $v=[regex]::Match($text,'(?m)^version=(\d+\.\d+\.\d+)$');$b=[regex]::Match($text,'(?m)^build=(\d{10})$')
  if(!$v.Success -or !$b.Success){throw 'Manifest release identifiers missing.'}
  & "$root/tests/ota_package_test.ps1" -FirmwareBin $bin -Version $v.Groups[1].Value -BuildNumber ([UInt64]$b.Groups[1].Value) -InstalledBuild 0
  $image=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $bin)))
  $capable=$image.Contains('FSGWREMOTE1') -and $image.Contains('FSGWREQUEST1')
  if(!$capable -and !$AllowLegacyTarget){throw 'This image has no remote protocol marker. A legacy downgrade loses remote control/result reporting. Use local OTA, or explicitly acknowledge with -AllowLegacyTarget.'}
  $entry.version=$v.Groups[1].Value;$entry.build=$b.Groups[1].Value;$entry.remote_capable=$capable
  $entry.url="$BaseUrl/gateway.ino.bin";$entry.bytes=(Get-Item -LiteralPath $bin).Length;$entry.sha256=(Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash.ToLowerInvariant()
  $entry.manifest_url="$BaseUrl/gateway.ino.manifest";$entry.manifest_bytes=(Get-Item -LiteralPath $manifest).Length;$entry.manifest_sha256=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
}else{
  if(!$Version){throw 'Specify the actual nRF firmware version using -Version.'}
  & "$root/tools/verify_nrf_legacy_dfu.ps1" -PackageZip $PackagePath -Version $Version
  $name=[IO.Path]::GetFileName($PackagePath)
  if($name -notmatch '^[a-zA-Z0-9_.-]+\.zip$'){throw 'Use a URL-safe ZIP filename without spaces; preserve the original package contents.'}
  $entry.version=$Version;$entry.url="$BaseUrl/$name";$entry.bytes=(Get-Item -LiteralPath $PackagePath).Length;$entry.sha256=(Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
}
if(!$OutputFile){$OutputFile=Join-Path $root "data/remote-release-$ReleaseId.json"}
if(Test-Path -LiteralPath $OutputFile){throw 'Output already exists; it will not be overwritten.'}
$json=$entry|ConvertTo-Json -Depth 5
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputFile),$json)
Write-Output "Catalog entry: $([IO.Path]::GetFullPath($OutputFile))"
Write-Output 'No upload/deployment performed. Upload the original artifacts to the chosen HTTPS store; register this entry in REMOTE_UPDATE_CATALOG. The store must support Range/206 and Content-Length, with no redirect or compression.'
