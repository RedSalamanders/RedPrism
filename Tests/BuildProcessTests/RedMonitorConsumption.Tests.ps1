<#
.SYNOPSIS
Exercises offline RedMonitor client verification with isolated immutable-file fixtures.
#>
[CmdletBinding()]
param([string]$RepositoryRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $RepositoryRoot){
 $RepositoryRoot=$PSScriptRoot
 while(-not(Test-Path -LiteralPath (Join-Path $RepositoryRoot 'Dependencies/RedMonitor.lock.json'))){
  $parent=Split-Path $RepositoryRoot
  if(-not $parent -or $parent -eq $RepositoryRoot){throw 'Cannot locate consumer root.'}
  $RepositoryRoot=$parent
 }
}
$verify=Join-Path $RepositoryRoot 'Verify-RedMonitorClient.ps1'
if(-not(Test-Path -LiteralPath $verify)){$verify=Join-Path $RepositoryRoot 'Tools/Verify-RedMonitorClient.ps1'}
$fixture=Join-Path $RepositoryRoot ('.build/client-sdk-tests/Unicode 日 space/'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory((Join-Path $fixture 'Dependencies'))|Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixture 'ThirdParty'))|Out-Null
Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'ThirdParty/RedMonitor') -Destination (Join-Path $fixture 'ThirdParty') -Recurse
$lockPath=Join-Path $fixture 'Dependencies/RedMonitor.lock.json'
$baseline=[IO.File]::ReadAllText((Join-Path $RepositoryRoot 'Dependencies/RedMonitor.lock.json'))
[IO.File]::WriteAllText($lockPath,$baseline)
$results=[Collections.Generic.List[object]]::new()
function Invoke-Control([bool]$Pass,[string]$Name,[string]$ExpectedError){
 $start=[Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
 foreach($arg in @('-NoProfile','-File',$verify,'-RepositoryRoot',$fixture)){$start.ArgumentList.Add($arg)}
 $start.Environment['PATH']=Join-Path $env:SystemRoot 'System32'
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start
 try{
  if(-not $process.Start()){throw 'Cannot start verification control.'}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit(30000)){$process.Kill($true);$process.WaitForExit();throw 'Owned verification control timed out.'}
  $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText((Join-Path $fixture ($Name+'.log')), $output)
  if($ExpectedError -and -not $output.Contains($ExpectedError)){throw "Client control $Name omitted '$ExpectedError':`n$output"}
  $results.Add([pscustomobject]@{name=$Name;expectedSuccess=$Pass;exitCode=$process.ExitCode})
  if(($process.ExitCode -eq 0) -ne $Pass){throw "Client control failed: $Name"}
  if(Test-Path -LiteralPath (Join-Path $fixture '.build')){throw 'Verification created build/cache output.'}
  Write-Host "PASS RedMonitor client $Name"
 }finally{$process.Dispose()}
}
Invoke-Control $true 'offline-exact-bytes'
# Use a self-contained command checkout for the tool integrity controls.
$bundle = Join-Path $fixture 'Dependencies/RedMonitorClientTools'
$build = Join-Path $fixture 'Build'
[IO.Directory]::CreateDirectory($build) | Out-Null
Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'Dependencies/RedMonitorClientTools') -Destination $bundle -Recurse
Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'Build/RedMonitorClientTools.psm1') -Destination $build
Copy-Item -LiteralPath $verify -Destination $fixture
$checkoutVerifier = $verify
$verify = Join-Path $fixture 'Verify-RedMonitorClient.ps1'
Invoke-Control $true 'offline-exact-tool-bundle'
foreach ($name in @('Verify-RedMonitorClient.ps1', 'Update-RedMonitorClient.ps1')) {
 $tool = Join-Path $bundle $name
 $toolBytes = [IO.File]::ReadAllBytes($tool)
 [IO.File]::AppendAllText($tool, [Environment]::NewLine + '# fixture alteration')
 Invoke-Control $false ('refuses-altered-tool-' + $name) -ExpectedError "RedMonitor client tooling hash mismatch: $name"
 [IO.File]::WriteAllBytes($tool, $toolBytes)
}
$verify = $checkoutVerifier
foreach($relative in @('include/RedMonitor/Client.h','LICENSE.txt')){
 $path=Join-Path $fixture ('ThirdParty/RedMonitor/'+$relative)
 $bytes=[IO.File]::ReadAllBytes($path)
 [IO.File]::AppendAllText($path,'corrupt')
 Invoke-Control $false ('tampered-'+[IO.Path]::GetFileName($path))
 [IO.File]::WriteAllBytes($path,$bytes)
 Move-Item -LiteralPath $path -Destination ($path+'.saved')
 Invoke-Control $false ('missing-'+[IO.Path]::GetFileName($path))
 Move-Item -LiteralPath ($path+'.saved') -Destination $path
}
foreach($field in @('repository','commit','apiRevision')){
 $pin=$baseline|ConvertFrom-Json
 $pin.$field=if($field -eq 'apiRevision'){99}else{'foreign'}
 $pin|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $lockPath
 Invoke-Control $false ('invalid-'+$field)
}
foreach($kind in @('duplicate','traversal','hash')){
 $pin=$baseline|ConvertFrom-Json
 switch($kind){
  duplicate {$pin.files[1]=$pin.files[0]}
  traversal {$pin.files[0].path='../Client.h'}
  hash {$pin.files[0].sha256='invalid'}
 }
 $pin|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $lockPath
 Invoke-Control $false ('invalid-'+$kind)
}
[IO.File]::WriteAllText($lockPath,$baseline)
$vendor=Join-Path $fixture 'ThirdParty/RedMonitor'
Move-Item -LiteralPath $vendor -Destination ($vendor+'.saved')
New-Item -ItemType Junction -Path $vendor -Target ($vendor+'.saved')|Out-Null
Invoke-Control $false 'reparse-vendor'
# Retain the entire fixture, including its contained reparse control, for diagnosis.
$results|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $fixture 'receipt.json')
Write-Host "PASS $($results.Count) offline client controls; retained $fixture"