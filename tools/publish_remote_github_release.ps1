param(
  [Parameter(Mandatory=$true)][ValidateSet('esp32','nrf')][string]$Kind,
  [Parameter(Mandatory=$true)][string]$PackagePath,
  [Parameter(Mandatory=$true)][string]$Tag,
  [string]$Version,
  [switch]$Publish
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 is required.'}
if($Tag -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,39}$' -or $Tag.Contains('..')){throw 'Use a unique, URL-safe release tag.'}
$root=Split-Path $PSScriptRoot -Parent
$origin=(& git -C $root remote get-url origin).Trim()
if($LASTEXITCODE -ne 0 -or $origin -notmatch '^https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)(?:\.git)?$'){throw 'Origin must be an HTTPS GitHub repository.'}
$repo=$Matches[1] -replace '\.git$',''
$id=($Kind+'-'+$Tag.Replace('.','_'))
if($id.Length -gt 48){throw 'Release ID exceeds the cloud catalog limit.'}
$package=(Resolve-Path -LiteralPath $PackagePath).Path
$holdDir=if($Kind -eq 'esp32'){$package}else{Split-Path $package -Parent}
if(Get-ChildItem -LiteralPath $holdDir -Filter 'HOLD_*.txt' -File -ErrorAction SilentlyContinue){
  throw 'Package is held pending TLS review or hardware testing; no GitHub Release upload allowed.'
}
$base="https://github.com/$repo/releases/download/$Tag"
$catalog=Join-Path $root "data/remote-release-$id.json"
$candidate=Join-Path $root ('data/remote-release-'+$id+'-'+[guid]::NewGuid().ToString('N')+'.json')
$prepare=@{Kind=$Kind;PackagePath=$package;ReleaseId=$id;BaseUrl=$base;OutputFile=$candidate}
if($Kind -eq 'nrf'){$prepare.Version=$Version}
& (Join-Path $PSScriptRoot 'prepare_remote_release.ps1') @prepare
if($LASTEXITCODE -ne 0){throw 'Package verification failed; nothing was uploaded.'}
if(Test-Path -LiteralPath $catalog){
  if([IO.File]::ReadAllText($catalog) -cne [IO.File]::ReadAllText($candidate)){throw 'Existing catalog entry differs; refusing to overwrite it.'}
  Remove-Item -LiteralPath $candidate
}else{Move-Item -LiteralPath $candidate -Destination $catalog}
$assets=if($Kind -eq 'esp32'){@((Join-Path $package 'gateway.ino.bin'),(Join-Path $package 'gateway.ino.manifest'))}else{@($package)}
if(!$Publish){Write-Output "Prepared only: $catalog. Pass -Publish after approving a public GitHub Release upload.";return}
$catalogAll=Join-Path $root 'data/remote-update-catalog.json'
$entries=if(Test-Path -LiteralPath $catalogAll){@([IO.File]::ReadAllText($catalogAll)|ConvertFrom-Json -AsHashtable)}else{@()}
$entry=[IO.File]::ReadAllText($catalog)|ConvertFrom-Json -AsHashtable
if(@($entries|Where-Object {$_.id -eq $entry.id}).Count){throw 'Release ID already appears in the combined catalog; refusing to replace it.'}
if($entries.Count -ge 8){throw 'Cloud catalog already has eight releases; no upload performed.'}
$gh=Get-Command gh -ErrorAction SilentlyContinue
$ghPath=if($gh){$gh.Source}else{Join-Path $env:ProgramFiles 'GitHub CLI/gh.exe'}
if(!(Test-Path -LiteralPath $ghPath)){throw "GitHub CLI (gh) is not installed. No upload occurred; prepared catalog: $catalog"}
if(!(Get-Command curl.exe -ErrorAction SilentlyContinue)){throw 'curl.exe is required to verify the published HTTPS Range response; no upload occurred.'}
& $ghPath auth status 2>$null
if($LASTEXITCODE -ne 0){throw 'GitHub CLI is not logged in. No upload occurred.'}
$verify=Join-Path $root ('data/remote-release-verify-'+$id+'-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($verify)|Out-Null
$toUpload=@($assets)
$release=& $ghPath release view $Tag --repo $repo --json assets,isDraft 2>$null
if($LASTEXITCODE -eq 0){
  $info=$release|ConvertFrom-Json
  $existing=@($info.assets|ForEach-Object name)
  foreach($asset in $assets){
    $name=[IO.Path]::GetFileName($asset)
    if($existing -contains $name){
      & $ghPath release download $Tag --repo $repo --pattern $name --dir $verify
      if($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath (Join-Path $verify $name) -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash){throw "Existing $name differs or cannot be verified; refusing to overwrite it."}
      $toUpload=@($toUpload|Where-Object {$_ -ne $asset})
    }
  }
}else{
  & $ghPath release create $Tag --repo $repo --draft --title $Tag --notes 'Firmware assets; verify the catalog SHA-256 before approving an update.'
  if($LASTEXITCODE -ne 0){throw 'Could not create draft release. No assets were uploaded.'}
}
if($toUpload.Count){
  & $ghPath release upload $Tag @toUpload --repo $repo
  if($LASTEXITCODE -ne 0){throw "Upload incomplete. Rerun this command to verify existing assets and upload missing ones; catalog remains local: $catalog"}
}
foreach($asset in $assets){
  $name=[IO.Path]::GetFileName($asset)
  if(!(Test-Path -LiteralPath (Join-Path $verify $name))){
    & $ghPath release download $Tag --repo $repo --pattern $name --dir $verify
    if($LASTEXITCODE -ne 0){throw "Could not read back $name. Release remains draft; verification files: $verify"}
  }
  if((Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $verify $name) -Algorithm SHA256).Hash){throw "Uploaded $name hash differs. Release remains draft."}
}
& $ghPath release edit $Tag --repo $repo --draft=false
if($LASTEXITCODE -ne 0){throw "Assets uploaded but release publication failed. Inspect the draft release; catalog remains local: $catalog"}
foreach($asset in $assets){
  $name=[IO.Path]::GetFileName($asset);$size=(Get-Item -LiteralPath $asset).Length
  $headerFile=Join-Path $verify ($name+'.headers');$rangeFile=Join-Path $verify ($name+'.range')
  & curl.exe --silent --show-error --max-time 45 --location --max-redirs 1 --proto '=https' --proto-redir '=https' `
    -H 'Range: bytes=0-31' -H 'Accept-Encoding: identity' -D $headerFile -o $rangeFile "$base/$name"
  if($LASTEXITCODE -ne 0){throw "Published $name could not be downloaded by HTTPS. Catalog was not updated."}
  $headers=[IO.File]::ReadAllText($headerFile)
  $statuses=[regex]::Matches($headers,'(?m)^HTTP/[^ ]+ (\d{3})')
  $lengths=[regex]::Matches($headers,'(?im)^Content-Length:\s*(\d+)')
  $ranges=[regex]::Matches($headers,'(?im)^Content-Range:\s*([^\r\n]+)')
  if($statuses.Count -lt 2 -or $statuses[0].Groups[1].Value -ne '302' -or $statuses[$statuses.Count-1].Groups[1].Value -ne '206' -or
     $lengths.Count -lt 1 -or $lengths[$lengths.Count-1].Groups[1].Value -ne '32' -or
     $ranges.Count -lt 1 -or $ranges[$ranges.Count-1].Groups[1].Value -ne "bytes 0-31/$size" -or
     (Get-Item -LiteralPath $rangeFile).Length -ne 32){throw "Published $name failed the exact Range/206 check. Catalog was not updated."}
}
[IO.File]::WriteAllText($catalogAll,(ConvertTo-Json -InputObject (@($entries)+@($entry)) -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output "Release published: $base"
Write-Output "Copy the contents of $catalogAll into REMOTE_UPDATE_CATALOG (no JSON editing needed). No device command was sent."
