# Execute the actual ordinary/analysis launch branch at a recording process boundary.
# Shared engine controls own real child/environment transport; no compiler is invoked here.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'build.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Actual native build entrypoint did not parse.'}
$calls=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Invoke-CiNativeCommand'},$true))
if($calls.Count -ne 3){throw 'Every SDK, ordinary and analysis launch must use the shared native bridge.'}
if($ast.Extent.Text -match '&\s*\$msbuild\b'){throw 'A direct MSBuild launch bypassed the private child boundary.'}
$branches=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.IfStatementAst] -and $node.Extent.Text.StartsWith('if ($Analyze)') -and $node.Extent.Text.Contains('$previousEspExtensions')},$true))
if($branches.Count -ne 1){throw 'Expected one actual ordinary/analysis native-launch branch.'}
$branch=[scriptblock]::Create($branches[0].Extent.Text)
foreach($analysis in @($false,$true)){
    & {
        param($body,$Analyze,$repoRoot)
        $msbuild='C:/synthetic tools/MSBuild.exe'
        $arguments=@('','literal [path] 日本語','/p:Value=a;b','quote"value','trailing\')
        $probe=@{calls=0;file='';directory='';arguments=@()}
        function Invoke-CiNativeCommand {
            param($FilePath,$WorkingDirectory,$Arguments)
            $probe.calls++;$probe.file=$FilePath;$probe.directory=$WorkingDirectory;$probe.arguments=@($Arguments)
            $global:LASTEXITCODE=19
        }
        $previous=[Environment]::GetEnvironmentVariable('Esp.Extensions','Process')
        Set-Item Env:Esp.Extensions -Value 'ExistingAnalyzer.dll'
        try{
            . $body
            if($probe.calls -ne 1 -or $probe.file -cne $msbuild -or $probe.directory -cne $repoRoot -or $exitCode -ne 19){throw 'Actual branch changed launch count, executable, location or native failure exit.'}
            if(($probe.arguments|ConvertTo-Json -Compress) -cne ($arguments|ConvertTo-Json -Compress)){throw 'Actual branch changed literal native arguments.'}
            if([Environment]::GetEnvironmentVariable('Esp.Extensions','Process') -cne 'ExistingAnalyzer.dll'){throw 'Analysis launch did not restore caller extension state.'}
        }finally{
            if($null -eq $previous){Remove-Item Env:Esp.Extensions -ErrorAction SilentlyContinue}
            else{[Environment]::SetEnvironmentVariable('Esp.Extensions',$previous,'Process')}
            $global:LASTEXITCODE=0
        }
    } $branch $analysis $root
}
Write-Host 'PASS all three native launch routes, actual ordinary/analysis literal arguments, exit and parent analyzer state; recording boundary only.'
