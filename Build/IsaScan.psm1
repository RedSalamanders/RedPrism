Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'VisualStudioDiscovery.psm1') -Force

function Find-RedPrismIsaTools {
    $installation = Find-RedPrismVisualStudioInstallationPath
    $directories = @()
    if ($installation) {
        $directories += Join-Path $installation 'VC\Tools\Llvm\x64\bin'
        $directories += Join-Path $installation 'VC\Tools\Llvm\bin'
    }
    $directories += Join-Path $env:ProgramFiles 'LLVM\bin'
    $pathCommand = Get-Command llvm-objdump.exe -ErrorAction SilentlyContinue
    if ($pathCommand) { $directories += Split-Path -Parent $pathCommand.Source }
    foreach ($directory in $directories) {
        $objdump = Join-Path $directory 'llvm-objdump.exe'
        $pdbutil = Join-Path $directory 'llvm-pdbutil.exe'
        if ((Test-Path -LiteralPath $objdump -PathType Leaf) -and (Test-Path -LiteralPath $pdbutil -PathType Leaf)) {
            return @{ Objdump = $objdump; Pdbutil = $pdbutil }
        }
    }
    throw 'The x64 ISA gate requires llvm-objdump.exe and llvm-pdbutil.exe. Install the Visual Studio C++ Clang tools component.'
}

function Invoke-RedPrismIsaScan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Binary,
        [Parameter(Mandatory)] [string] $OutputDirectory,
        [string] $Pdb = [IO.Path]::ChangeExtension($Binary, '.pdb')
    )
    if (-not (Test-Path -LiteralPath $Binary -PathType Leaf) -or -not (Test-Path -LiteralPath $Pdb -PathType Leaf)) {
        throw "ISA scan needs the linked image and its full PDB: $Binary / $Pdb"
    }
    $tools = Find-RedPrismIsaTools
    if (-not ('RedPrismBuild.IsaScanner' -as [type])) {
        Add-Type -Path (Join-Path $PSScriptRoot 'IsaScan.cs')
    }
    $allowlist = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'IsaRuntimeAllowlist.psd1')
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $metadata = Join-Path $OutputDirectory 'contributions.txt'
    $symbols = Join-Path $OutputDirectory 'symbols.txt'
    $disassembly = Join-Path $OutputDirectory 'disassembly.txt'
    & $tools.Pdbutil dump --summary --modules --section-contribs $Pdb > $metadata
    if ($LASTEXITCODE -ne 0) { throw "PDB contribution extraction failed: $Pdb" }
    & $tools.Pdbutil dump --symbols $Pdb > $symbols
    if ($LASTEXITCODE -ne 0) { throw "PDB function extraction failed: $Pdb" }
    & $tools.Objdump -d --x86-asm-syntax=intel $Binary > $disassembly
    if ($LASTEXITCODE -ne 0) { throw "Instruction disassembly failed: $Binary" }
    $result = [RedPrismBuild.IsaScanner]::Scan($Binary, $metadata, $symbols, $disassembly,
        [string[]] $allowlist.Objects, [string[]] $allowlist.Libraries, [string[]] $allowlist.Functions)
    $report = [ordered]@{
        binary = [IO.Path]::GetFullPath($Binary)
        binarySha256 = (Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash
        pdbSha256 = (Get-FileHash -LiteralPath $Pdb -Algorithm SHA256).Hash
        allowlistSha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'IsaRuntimeAllowlist.psd1') -Algorithm SHA256).Hash
        decoder = $tools.Objdump
        decodedInstructions = $result.Instructions
        vectorInstructions = $result.VectorInstructions
        evexInstructions = $result.EvexInstructions
        tierInstructions = $result.TierInstructions
        tierEvexInstructions = $result.TierEvexInstructions
        runtimeInstructions = $result.RuntimeInstructions
        undecodedPrefixes = $result.UndecodedPrefixes
        inlineTableVectorData = $result.InlineTableVectorData
        violations = @($result.Violations)
    }
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'report.json') -Encoding utf8
    if ($result.Violations.Count -ne 0) {
        throw "ISA scan rejected $Binary`n$($result.Violations -join "`n")"
    }
    Write-Host "PASS isa-scan.$([IO.Path]::GetFileName($Binary)): $($result.VectorInstructions) VEX/EVEX instructions ($($result.TierInstructions) tier, $($result.RuntimeInstructions) runtime); $($result.UndecodedPrefixes) undecoded data prefixes, $($result.InlineTableVectorData) verified table data."
    return $result
}

Export-ModuleMember -Function Find-RedPrismIsaTools, Invoke-RedPrismIsaScan
