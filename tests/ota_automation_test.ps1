$ErrorActionPreference='Stop'
. "$PSScriptRoot/../tools/build_esp32_ota_package.ps1"
function Check($condition,$message){if(!$condition){throw $message}}
$text = "// user edit`r`n#define FSGW_VERSION `"2.0.14`"`r`n#define FSGW_BUILD 2026102616`r`n// preserve me`r`n"
$plan=Get-ReleasePlan $text '' @(2026102618) ([datetime]'2026-10-26')
Check ($plan.Version -eq '2.0.15' -and $plan.Build -eq '2026102619') 'automatic patch/build'
Check ($plan.Text.Contains('// user edit') -and $plan.Text.EndsWith("// preserve me`r`n")) 'user edits preserved'
$custom=Get-ReleasePlan $text '2.1.0' @() ([datetime]'2026-10-27')
Check ($custom.Version -eq '2.1.0' -and $custom.Build -eq '2026102701') 'custom version/new day'
$roll=Get-ReleasePlan $text '' @(2026103199) ([datetime]'2026-10-26')
Check ($roll.Build -eq '2026110101') 'day rollover and backwards clock'
foreach($bad in @('2.0.14','2.0.13','invalid')){
 $failed=$false;try{Get-ReleasePlan $text $bad @()|Out-Null}catch{$failed=$true}
 Check $failed 'bad version accepted'
}
$fixture=Join-Path (Split-Path $PSScriptRoot) ('data/ota-automation-test-'+[guid]::NewGuid().ToString('N'))
$source=Join-Path $fixture 'firmware/esp32s3/gateway/gateway.ino'
[IO.Directory]::CreateDirectory((Split-Path $source))|Out-Null
[IO.File]::WriteAllText($source,$text)
$before=(Get-FileHash $source).Hash
$cli=Join-Path $fixture 'mock-cli.ps1'
[IO.File]::WriteAllText($cli,@'
if($args[0] -eq 'core'){
 '{"platforms":[{"id":"esp32:esp32","installed_version":"3.3.12"}]}'
 $global:LASTEXITCODE=0
}else{$global:LASTEXITCODE=17}
'@)
$failed=$false;try{Invoke-AutomatedRelease $fixture '' $cli}catch{$failed=$_.Exception.Message.Contains('key missing')}
Check $failed 'missing key must stop before compile'
[IO.Directory]::CreateDirectory((Join-Path $fixture 'secrets'))|Out-Null
# Placeholder only in an isolated failure fixture, never used for signing.
[IO.File]::WriteAllText((Join-Path $fixture 'secrets/esp32-ota-signing-key.pem'),'not-a-key')
$failed=$false;try{Invoke-AutomatedRelease $fixture '' $cli}catch{$failed=$_.Exception.Message.Contains('Compilation failed')}
Check $failed 'compile failure must propagate'
Check ((Get-FileHash $source).Hash -eq $before) 'failed compile must restore byte-exact user source'
Check (!(Test-Path (Join-Path $fixture 'dist'))) 'failed compile published a package'
[IO.File]::WriteAllText($cli,@'
if($args[0] -eq 'core'){
 '{"platforms":[{"id":"esp32:esp32","installed_version":"3.3.12"}]}'
}else{
 $sketch=$args[-1]
 [IO.File]::AppendAllText((Join-Path $sketch 'gateway.ino'),"`n// concurrent user edit")
}
$global:LASTEXITCODE=0
'@)
$failed=$false;try{Invoke-AutomatedRelease $fixture '' $cli}catch{$failed=$_.Exception.Message.Contains('Source changed')}
Check $failed 'concurrent source edit must reject packaging'
Check ([IO.File]::ReadAllText($source).Contains('// concurrent user edit')) 'concurrent edit was overwritten'
Check (!(Test-Path (Join-Path $fixture 'dist'))) 'source mismatch published a package'
'PASS automatic version/build, custom version, duplicates, rollover, missing key, compile failure and source preservation'
