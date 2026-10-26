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
foreach($bad in @('2.0.13','invalid')){
 $failed=$false;try{Get-ReleasePlan $text $bad @()|Out-Null}catch{$failed=$true}
 Check $failed 'bad version accepted'
}
$resume=Get-ReleasePlan "#define FSGW_VERSION `"2.0.17`"`r`n#define FSGW_BUILD 2026102702`r`n" '2.0.17' @() ([datetime]'2026-10-27')
Check ($resume.Version -eq '2.0.17' -and $resume.Build -eq '2026102702') 'unfinished manual release resumes same build'
$published=Get-ReleasePlan "#define FSGW_VERSION `"2.0.17`"`r`n#define FSGW_BUILD 2026102702`r`n" '2.0.17' @(2026102702) ([datetime]'2026-10-27')
Check ($published.Build -eq '2026102703') 'published build is never reused'
'PASS release planner: automatic patch, custom version, monotonic build, rollover and manual resume'
