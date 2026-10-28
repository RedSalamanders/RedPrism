#Requires -Version 7.0
# Companion to the metadata grammar: declared local Markdown links and literal repository PowerShell
# examples have separate checks. No examples, linked documents or target scripts are executed.
Set-StrictMode -Version Latest

function Assert-RedPrismOrdinarySkillPath {
    param([string]$Path)
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        $attributes = $null
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch [IO.FileNotFoundException] { }
        catch [IO.DirectoryNotFoundException] { }
        if ($null -ne $attributes -and ($attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse skill path: $current" }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Get-RedPrismPackageMarkdown {
    param([string]$Path)
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Path)
    while ($pending.Count) {
        $directory = $pending.Pop()
        Assert-RedPrismOrdinarySkillPath $directory
        foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
            Assert-RedPrismOrdinarySkillPath $entry.FullName
            if ($entry.PSIsContainer) { $pending.Push($entry.FullName) }
            elseif ($entry.Extension -ieq '.md') { $entry }
        }
    }
}

function Get-RedPrismDeclaredSkillLink {
    param([string]$Prose)
    # A blank line ends this bounded inline-code span; it cannot hide links in another paragraph.
    $text = [regex]::Replace($Prose,'(?<![\\`])(`+)(?!`)(?:(?!\r?\n[ \t]*\r?\n)[\s\S])*?(?<!`)\1(?!`)',' ')
    $destination = '(?:<(?<angle>[^>\r\n]*)>|(?<plain>(?:\\[()]|\\(?![()])|[^()\s\\]|\((?<depth>)|\)(?<-depth>))+(?(depth)(?!))))'
    $title = '(?:[ \t]+(?:"[^"\r\n]*"|''[^''\r\n]*''|\([^()\r\n]*\)))?[ \t]*'
    foreach ($link in [regex]::Matches($text,('\]\([ \t]*'+$destination+$title+'\)'))) {
        $target = if ($link.Groups['angle'].Success) { $link.Groups['angle'].Value } else { $link.Groups['plain'].Value }
        if ($target) { $target -replace '\\([()])','$1' }
    }
    $definitions = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $definitionPattern = '(?m)^[ ]{0,3}\[(?<label>[^\]\r\n]+)\]:[ \t]*(?:\r?\n[ \t]*)?'+$destination+$title+'$'
    foreach ($link in [regex]::Matches($text,$definitionPattern)) {
        $label = [regex]::Replace($link.Groups['label'].Value.Trim(),'\s+',' ')
        if ($definitions.ContainsKey($label)) { continue } # CommonMark uses the first definition.
        $target = if ($link.Groups['angle'].Success) { $link.Groups['angle'].Value } else { $link.Groups['plain'].Value }
        $definitions.Add($label,$target)
        if ($target) { $target -replace '\\([()])','$1' }
    }
    $text = [regex]::Replace($text,$definitionPattern,' ')
    # A full/collapsed reference has adjacent brackets. Whitespace separates shortcut/text spans.
    foreach ($usage in [regex]::Matches($text,'\[(?<text>[^\]\r\n]+)\]\[(?<label>[^\]\r\n]*)\]')) {
        $label = if ($usage.Groups['label'].Value) { $usage.Groups['label'].Value } else { $usage.Groups['text'].Value }
        $label = [regex]::Replace($label.Trim(),'\s+',' ')
        if (-not $definitions.ContainsKey($label)) { throw "Unresolved Markdown reference label: $label" }
    }
}

function Get-RedPrismSkillReferencePath {
    param([string]$Root,[string]$Base,[string]$Target)
    if ($Target -match '^#|^//') { return $null }
    $local = ($Target -split '[#?]',2)[0]
    if ($local.StartsWith('file:',[StringComparison]::OrdinalIgnoreCase)) {
        $uri = $null
        if (-not [uri]::TryCreate($local,[UriKind]::Absolute,[ref]$uri) -or -not $uri.IsFile) { throw "Invalid local file URI: $Target" }
        $local = $uri.LocalPath
    } elseif ($local -match '^[a-zA-Z]:(?![\\/])') { throw "Drive-relative skill reference: $Target" }
    elseif ($local -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -and $local -notmatch '^[a-zA-Z]:[\\/]') { return $null }
    else { $local = [uri]::UnescapeDataString($local) }
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $path = [IO.Path]::GetFullPath([IO.Path]::Combine($Base,$local))
    $contained = $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or
        $path.Equals($prefix.TrimEnd('\','/'),[StringComparison]::OrdinalIgnoreCase)
    if (-not $contained) {
        throw "Missing or non-repository skill reference: $Target"
    }
    Assert-RedPrismOrdinarySkillPath $path
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing or non-repository skill reference: $Target" }
    return $path
}

function Test-RedPrismSkillPackageIntegrity {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Root)
    $skillRoot = Join-Path $Root '.agents/skills'
    Assert-RedPrismOrdinarySkillPath $skillRoot
    $folders = @(Get-ChildItem -LiteralPath $skillRoot -Directory -Force -ErrorAction Stop)
    if (-not $folders.Count) { throw 'No skill packages found' }
    $referenceCount = 0
    $commandCount = 0
    foreach ($folder in $folders) {
        if ($folder.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse skill folder: $($folder.Name)" }
        $entrypoint = Join-Path $folder.FullName 'SKILL.md'
        Assert-RedPrismOrdinarySkillPath $entrypoint
        if (-not [IO.File]::Exists($entrypoint)) { throw "Missing SKILL.md: $($folder.Name)" }
        foreach ($file in Get-RedPrismPackageMarkdown $folder.FullName) {
            $fence = $null
            $language = ''
            $prose = [Collections.Generic.List[string]]::new()
            foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
                $marker = [regex]::Match($line, '^ {0,3}(?:(?:[-+*]|\d+[.)])[ \t]+)?(?<fence>`{3,}|~{3,})(?<rest>.*)$')
                # Backtick fence info cannot contain backticks; a same-line code span is prose.
                if ($marker.Success -and ($null -ne $fence -or $marker.Groups['fence'].Value[0] -cne '`' -or
                    -not $marker.Groups['rest'].Value.Contains('`'))) {
                    $token = $marker.Groups['fence'].Value
                    if ($null -eq $fence) { $fence = $token; $language = $marker.Groups['rest'].Value.Trim() }
                    elseif ($token[0] -ceq $fence[0] -and $token.Length -ge $fence.Length -and -not $marker.Groups['rest'].Value.Trim()) {
                        $fence = $null; $language = ''
                    }
                    continue
                }
                if ($null -eq $fence) {
                    $prose.Add($line)
                } elseif ($language -ieq 'powershell' -and $line -match '^\s*\.([\\/])(?<script>[A-Za-z0-9_.\\/-]+\.ps1)(?:\s|$)') {
                    $target = Get-RedPrismSkillReferencePath -Root $Root -Base $Root -Target $Matches.script
                    $tokens = $null; $errors = $null
                    $example = [Management.Automation.Language.Parser]::ParseInput($line,[ref]$tokens,[ref]$errors)
                    if ($errors.Count) { throw "Invalid PowerShell skill example in $($file.FullName): $($errors[0].Message)" }
                    $scriptAst = [Management.Automation.Language.Parser]::ParseFile($target,[ref]$tokens,[ref]$errors)
                    if ($errors.Count) { throw "Referenced PowerShell script is invalid: $target" }
                    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
                    if ($null -ne $scriptAst.ParamBlock) {
                        foreach ($parameter in $scriptAst.ParamBlock.Parameters) {
                            [void]$names.Add($parameter.Name.VariablePath.UserPath)
                            foreach ($alias in $parameter.Attributes | Where-Object { $_.TypeName.Name -iin 'Alias','AliasAttribute','System.Management.Automation.AliasAttribute' }) {
                                foreach ($argument in $alias.PositionalArguments) {
                                    if ($argument -is [Management.Automation.Language.StringConstantExpressionAst]) { [void]$names.Add($argument.Value) }
                                }
                            }
                        }
                    }
                    $bindings = @(if ($null -ne $scriptAst.ParamBlock) { $scriptAst.ParamBlock.Attributes | Where-Object { $_.TypeName.Name -iin 'CmdletBinding','CmdletBindingAttribute','System.Management.Automation.CmdletBindingAttribute' } })
                    if ($bindings.Count) {
                        # Inspect only the installed built-in's common-parameter metadata, never the target script.
                        $common = (Get-Command Microsoft.PowerShell.Utility\Write-Output).Parameters
                        foreach ($name in [Management.Automation.PSCmdlet]::CommonParameters) {
                            [void]$names.Add($name)
                            if ($common.ContainsKey($name)) { foreach ($alias in $common[$name].Aliases) { [void]$names.Add($alias) } }
                        }
                        foreach ($binding in $bindings) { foreach ($argument in $binding.NamedArguments) {
                            if ($argument.ArgumentName -ine 'SupportsShouldProcess') { continue }
                            $enabled = $argument.ExpressionOmitted -or
                                ($argument.Argument -is [Management.Automation.Language.VariableExpressionAst] -and $argument.Argument.VariablePath.UserPath -ieq 'true') -or
                                ($argument.Argument -is [Management.Automation.Language.ConstantExpressionAst] -and $argument.Argument.Value -is [bool] -and $argument.Argument.Value)
                            if ($enabled) { [void]$names.Add('WhatIf'); [void]$names.Add('Confirm') }
                        } }
                    }
                    $command = @($example.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] },$false))[0]
                    foreach ($argument in $command.CommandElements | Where-Object { $_ -is [Management.Automation.Language.CommandParameterAst] }) {
                        if (-not $names.Contains($argument.ParameterName)) { throw "Unknown skill example parameter -$($argument.ParameterName) for $target" }
                    }
                    $commandCount++
                }
            }
            foreach ($target in Get-RedPrismDeclaredSkillLink ($prose -join "`n")) {
                $resolved = Get-RedPrismSkillReferencePath -Root $Root -Base $file.DirectoryName -Target $target
                if ($resolved) { $referenceCount++ }
            }
        }
    }
    Write-Host "Skill package references: PASS ($($folders.Count) packages, $referenceCount local links, $commandCount literal script signatures)."
    Write-Host 'Metadata grammar uses its own validator. Parameter values, example execution and instruction quality: NOT_EVALUATED.'
}

Export-ModuleMember -Function Test-RedPrismSkillPackageIntegrity
