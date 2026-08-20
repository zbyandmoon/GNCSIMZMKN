Set-StrictMode -Version Latest

function New-SourceBoundaryFinding {
    param(
        [Parameter(Mandatory = $true)][string]$RuleId,
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Line = 0,
        [Parameter(Mandatory = $true)][string]$Message
    )

    return [PSCustomObject]([ordered]@{
        rule_id = $RuleId
        path = $Path
        line = $Line
        message = $Message
    })
}

function ConvertTo-SourceBoundaryPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $normalized = $Path.Replace('\', '/').Trim().TrimStart('/')
    $segments = [System.Collections.Generic.List[string]]::new()
    foreach ($segment in @($normalized -split '/')) {
        if ([string]::IsNullOrEmpty($segment) -or $segment -ceq '.') { continue }
        if ($segment -ceq '..') {
            if ($segments.Count -gt 0 -and $segments[$segments.Count - 1] -cne '..') {
                $segments.RemoveAt($segments.Count - 1)
            }
            else {
                [void]$segments.Add($segment)
            }
            continue
        }
        [void]$segments.Add($segment)
    }
    return $segments -join '/'
}

function Get-TransitiveModuleDependencies {
    param(
        [Parameter(Mandatory = $true)][string]$ModuleName,
        [Parameter(Mandatory = $true)][object[]]$Edges,
        [Parameter(Mandatory = $true)][string[]]$ModuleOrder
    )

    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $pending = [System.Collections.Generic.Stack[string]]::new()
    foreach ($edge in $Edges) {
        if ([string]$edge.source -ieq $ModuleName) {
            $pending.Push([string]$edge.target)
        }
    }
    while ($pending.Count -gt 0) {
        $candidate = $pending.Pop()
        if (-not $seen.Add($candidate)) { continue }
        foreach ($edge in $Edges) {
            if ([string]$edge.source -ieq $candidate) {
                $pending.Push([string]$edge.target)
            }
        }
    }
    return @($ModuleOrder | Where-Object { $seen.Contains($_) })
}

function New-SourceBoundaryPolicy {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$Registry,
        [Parameter(Mandatory = $true)]$Dependency
    )

    $moduleNames = @($Dependency.ModuleNames | ForEach-Object { [string]$_ })
    $sourceRoots = @($Registry.modules | ForEach-Object {
        [PSCustomObject]([ordered]@{
            owner = [string]$_.name
            root = ConvertTo-SourceBoundaryPath -Path ([string]$_.source_root)
        })
    })

    $allowedModulesByOwner = @{}
    foreach ($moduleName in $moduleNames) {
        $allowedModulesByOwner[$moduleName] = @(
            $moduleName
            Get-TransitiveModuleDependencies `
                -ModuleName $moduleName `
                -Edges @($Dependency.ArchitectureEdges) `
                -ModuleOrder $moduleNames
        )
    }

    # Source seams are intentionally narrower than the transitive link graph.
    $allowedModulesByOwner['adapters'] = @('adapters', 'foundation', 'contracts', 'application', 'evidence')
    $allowedModulesByOwner['packages'] = @('foundation', 'contracts', 'model_sdk')
    $allowedModulesByOwner['apps'] = @('foundation', 'contracts', 'application', 'adapters')
    $allowedModulesByOwner['user'] = @($moduleNames)

    return [PSCustomObject]([ordered]@{
        ModuleNames = @($moduleNames)
        ArchitectureEdges = @($Dependency.ArchitectureEdges)
        SourceRoots = @($sourceRoots)
        AllowedModulesByOwner = $allowedModulesByOwner
        LegacyIdentifiers = @(
            'SimulationNode',
            'DiscreteNode',
            'NodeFactory',
            'NodeRegistry',
            'AssemblyContext',
            'MissionAssembler',
            'ConfigNode',
            'IObservable',
            'IDiscreteTask',
            'GNC_REGISTER_NODE_TYPE',
            'GNC_REGISTER_BUILTIN_NODE',
            'requireByName',
            'bindIfPresent'
        )
    })
}

function Get-PolicyOwnershipFindings {
    param([Parameter(Mandatory = $true)]$Policy)

    $findings = [System.Collections.Generic.List[object]]::new()
    $owners = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $roots = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Policy.SourceRoots)) {
        $owner = [string]$entry.owner
        $root = ConvertTo-SourceBoundaryPath -Path ([string]$entry.root)
        if (-not $owners.Add($owner)) {
            [void]$findings.Add((New-SourceBoundaryFinding `
                    -RuleId 'SB-OWNER-UNIQUE' `
                    -Path $root `
                    -Message "Source owner '$owner' is registered more than once."))
        }
        if (-not $roots.Add($root)) {
            [void]$findings.Add((New-SourceBoundaryFinding `
                    -RuleId 'SB-OWNER-UNIQUE' `
                    -Path $root `
                    -Message "Source root '$root' has more than one owner."))
        }
    }

    $entries = @($Policy.SourceRoots)
    for ($left = 0; $left -lt $entries.Count; ++$left) {
        $leftRoot = (ConvertTo-SourceBoundaryPath -Path ([string]$entries[$left].root)).TrimEnd('/')
        for ($right = $left + 1; $right -lt $entries.Count; ++$right) {
            $rightRoot = (ConvertTo-SourceBoundaryPath -Path ([string]$entries[$right].root)).TrimEnd('/')
            if ($leftRoot.Equals($rightRoot, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
            if ($leftRoot.StartsWith($rightRoot + '/', [System.StringComparison]::OrdinalIgnoreCase) -or
                $rightRoot.StartsWith($leftRoot + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
                [void]$findings.Add((New-SourceBoundaryFinding `
                        -RuleId 'SB-OWNER-UNIQUE' `
                        -Path "$leftRoot | $rightRoot" `
                        -Message 'Registered source roots overlap and cannot have a unique physical owner.'))
            }
        }
    }
    return @($findings)
}

function Get-PathClassification {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)]$Policy
    )

    $path = ConvertTo-SourceBoundaryPath -Path $RelativePath
    $sourceRoots = @($Policy.SourceRoots | Sort-Object { ([string]$_.root).Length } -Descending)
    foreach ($entry in $sourceRoots) {
        $root = (ConvertTo-SourceBoundaryPath -Path ([string]$entry.root)).TrimEnd('/')
        if ($path.Equals($root, [System.StringComparison]::OrdinalIgnoreCase) -or
            $path.StartsWith($root + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
            return [PSCustomObject]([ordered]@{
                kind = 'module'
                owner = [string]$entry.owner
                package = $null
            })
        }
    }

    foreach ($logicalRoot in @('packages', 'user', 'apps')) {
        if ($path.Equals($logicalRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
            $path.StartsWith($logicalRoot + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
            $packageName = $null
            if ($logicalRoot -eq 'packages') {
                $parts = @($path -split '/')
                if ($parts.Count -gt 1) { $packageName = [string]$parts[1] }
            }
            return [PSCustomObject]([ordered]@{
                kind = 'logical'
                owner = $logicalRoot
                package = $packageName
            })
        }
    }

    $frameworkSource = [regex]::Match(
        $path,
        '^(?i:framework/src)/(?<module>[A-Za-z_][A-Za-z0-9_]*)(?:/|$)')
    if ($frameworkSource.Success) {
        $module = [string]$frameworkSource.Groups['module'].Value
        $registered = @($Policy.ModuleNames | Where-Object {
                [string]$_.ToString() -ceq $module
            })
        if ($registered.Count -eq 1) {
            return [PSCustomObject]([ordered]@{
                kind = 'module'
                owner = $module
                package = $null
            })
        }
    }

    if ($path -match '^(?i:framework|adapters)(?:/|$)') {
        return [PSCustomObject]([ordered]@{ kind = 'unowned-production'; owner = $null; package = $null })
    }
    return [PSCustomObject]([ordered]@{ kind = 'external'; owner = $null; package = $null })
}

function Get-RepositoryRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$FullPath
    )

    $root = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $candidate = [System.IO.Path]::GetFullPath($FullPath)
    $prefix = $root + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path '$candidate' is outside repository root '$root'."
    }
    return ConvertTo-SourceBoundaryPath -Path $candidate.Substring($prefix.Length)
}

function Get-CxxLexicalViews {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)

    $directive = [System.Text.StringBuilder]::new()
    $code = [System.Text.StringBuilder]::new()
    $state = 'normal'
    $escaped = $false

    for ($index = 0; $index -lt $Text.Length; ++$index) {
        $character = [char]$Text[$index]
        $next = if ($index + 1 -lt $Text.Length) { [char]$Text[$index + 1] } else { [char]0 }
        $masked = if ($character -eq "`r" -or $character -eq "`n") { $character } else { [char]' ' }

        if ($state -eq 'line-comment') {
            if ($character -eq "`r" -or $character -eq "`n") {
                [void]$directive.Append($character)
                [void]$code.Append($character)
                $state = 'normal'
            }
            else {
                [void]$directive.Append(' ')
                [void]$code.Append(' ')
            }
            continue
        }
        if ($state -eq 'block-comment') {
            [void]$directive.Append($masked)
            [void]$code.Append($masked)
            if ($character -eq '*' -and $next -eq '/') {
                ++$index
                [void]$directive.Append(' ')
                [void]$code.Append(' ')
                $state = 'normal'
            }
            continue
        }
        if ($state -eq 'double-quoted' -or $state -eq 'single-quoted') {
            [void]$directive.Append($character)
            [void]$code.Append($masked)
            if ($escaped) {
                $escaped = $false
                continue
            }
            if ($character -eq '\') {
                $escaped = $true
                continue
            }
            if (($state -eq 'double-quoted' -and $character -eq '"') -or
                ($state -eq 'single-quoted' -and $character -eq "'")) {
                $state = 'normal'
            }
            continue
        }

        if ($character -eq '/' -and $next -eq '/') {
            [void]$directive.Append('  ')
            [void]$code.Append('  ')
            ++$index
            $state = 'line-comment'
            continue
        }
        if ($character -eq '/' -and $next -eq '*') {
            [void]$directive.Append('  ')
            [void]$code.Append('  ')
            ++$index
            $state = 'block-comment'
            continue
        }

        # Mask standard raw-string bodies so retired identifiers in diagnostics do not count as code use.
        if ($character -eq 'R' -and $next -eq '"') {
            $openParenthesis = $Text.IndexOf('(', $index + 2)
            if ($openParenthesis -ge 0 -and $openParenthesis -le $index + 18) {
                $delimiter = $Text.Substring($index + 2, $openParenthesis - ($index + 2))
                if ($delimiter -match '^[^ ()\\\t\r\n]{0,16}$') {
                    $endMarker = ')' + $delimiter + '"'
                    $markerIndex = $Text.IndexOf(
                        $endMarker,
                        $openParenthesis + 1,
                        [System.StringComparison]::Ordinal)
                    $segmentEnd = if ($markerIndex -ge 0) {
                        $markerIndex + $endMarker.Length
                    }
                    else {
                        $Text.Length
                    }
                    for ($rawIndex = $index; $rawIndex -lt $segmentEnd; ++$rawIndex) {
                        $rawCharacter = [char]$Text[$rawIndex]
                        [void]$directive.Append($rawCharacter)
                        if ($rawCharacter -eq "`r" -or $rawCharacter -eq "`n") {
                            [void]$code.Append($rawCharacter)
                        }
                        else {
                            [void]$code.Append(' ')
                        }
                    }
                    $index = $segmentEnd - 1
                    continue
                }
            }
        }

        [void]$directive.Append($character)
        if ($character -eq '"') {
            [void]$code.Append(' ')
            $state = 'double-quoted'
            $escaped = $false
        }
        elseif ($character -eq "'") {
            [void]$code.Append(' ')
            $state = 'single-quoted'
            $escaped = $false
        }
        else {
            [void]$code.Append($character)
        }
    }

    return [PSCustomObject]([ordered]@{
        DirectiveText = $directive.ToString()
        CodeText = $code.ToString()
    })
}

function Get-LegacyIdentifierFromInclude {
    param(
        [Parameter(Mandatory = $true)][string]$IncludePath,
        [Parameter(Mandatory = $true)]$Policy
    )

    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($IncludePath.Replace('\', '/'))
    $normalizedLeaf = ($leaf -replace '[-_]', '').ToLowerInvariant()
    foreach ($identifier in @($Policy.LegacyIdentifiers)) {
        if (([string]$identifier -replace '[-_]', '').ToLowerInvariant() -ceq $normalizedLeaf) {
            return [string]$identifier
        }
    }
    return $null
}

function Resolve-IncludeTarget {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$IncludePath,
        [Parameter(Mandatory = $true)][string]$Delimiter,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)]$Policy
    )

    $normalized = ConvertTo-SourceBoundaryPath -Path $IncludePath
    $legacyIdentifier = Get-LegacyIdentifierFromInclude -IncludePath $normalized -Policy $Policy
    if ($normalized -match '^(?i:gnc)/(?<module>[^/]+)(?:/|$)') {
        $module = @($Policy.ModuleNames | Where-Object { $_ -ieq $Matches['module'] } | Select-Object -First 1)
        if ($module.Count -eq 0) {
            return [PSCustomObject]([ordered]@{
                kind = 'unknown-internal'
                owner = $null
                package = $null
                legacy_identifier = $legacyIdentifier
            })
        }
        return [PSCustomObject]([ordered]@{
            kind = 'module'
            owner = [string]$module[0]
            package = $null
            legacy_identifier = $legacyIdentifier
        })
    }

    $directClassification = Get-PathClassification -RelativePath $normalized -Policy $Policy
    if ($directClassification.kind -ne 'external') {
        return [PSCustomObject]([ordered]@{
            kind = $directClassification.kind
            owner = $directClassification.owner
            package = $directClassification.package
            legacy_identifier = $legacyIdentifier
        })
    }

    if ($Delimiter -ceq '"') {
        try {
            $sourceDirectory = Split-Path -Path $SourcePath -Parent
            $nativeInclude = $normalized.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
            $candidate = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot (Join-Path $sourceDirectory $nativeInclude)))
            $relativeCandidate = Get-RepositoryRelativePath -RepoRoot $RepoRoot -FullPath $candidate
            $relativeClassification = Get-PathClassification -RelativePath $relativeCandidate -Policy $Policy
            if ($relativeClassification.kind -ne 'external') {
                return [PSCustomObject]([ordered]@{
                    kind = $relativeClassification.kind
                    owner = $relativeClassification.owner
                    package = $relativeClassification.package
                    legacy_identifier = $legacyIdentifier
                })
            }
        }
        catch {
            # An unresolved external relative include has no repository-owned dependency edge.
        }
    }

    return [PSCustomObject]([ordered]@{
        kind = 'external'
        owner = $null
        package = $null
        legacy_identifier = $legacyIdentifier
    })
}

function New-SourceInventoryRecord {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)]$Policy
    )

    $normalizedPath = ConvertTo-SourceBoundaryPath -Path $Path
    $classification = Get-PathClassification -RelativePath $normalizedPath -Policy $Policy
    $views = Get-CxxLexicalViews -Text $Content
    $directiveLines = @([regex]::Split($views.DirectiveText, "\r\n|\n|\r"))
    $codeLines = @([regex]::Split($views.CodeText, "\r\n|\n|\r"))
    $includes = [System.Collections.Generic.List[object]]::new()
    $legacyPaths = [System.Collections.Generic.List[object]]::new()
    $legacyIdentifiers = [System.Collections.Generic.List[object]]::new()

    for ($lineIndex = 0; $lineIndex -lt $directiveLines.Count; ++$lineIndex) {
        $line = [string]$directiveLines[$lineIndex]
        $normalizedLine = $line.Replace('\', '/')
        if ($normalizedLine -match '(?i)(?:^|[^A-Za-z0-9_])reference/legacy(?:/|[^A-Za-z0-9_]|$)' -or
            $normalizedLine -match '(?i)(?:^|[^A-Za-z0-9_])legacy-source\.zip(?:[^A-Za-z0-9_]|$)') {
            [void]$legacyPaths.Add([PSCustomObject]([ordered]@{ line = $lineIndex + 1; text = $line.Trim() }))
        }
        $includeMatch = [regex]::Match(
            $line,
            '^\s*#\s*include\s*(?<open>[<"])(?<path>[^>"]+)[>"]',
            [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if (-not $includeMatch.Success) { continue }
        $includePath = $includeMatch.Groups['path'].Value.Trim()
        $target = Resolve-IncludeTarget `
            -SourcePath $normalizedPath `
            -IncludePath $includePath `
            -Delimiter $includeMatch.Groups['open'].Value `
            -RepoRoot $RepoRoot `
            -Policy $Policy
        [void]$includes.Add([PSCustomObject]([ordered]@{
            line = $lineIndex + 1
            text = $includePath
            target_kind = $target.kind
            target_owner = $target.owner
            target_package = $target.package
            legacy_identifier = $target.legacy_identifier
        }))
    }

    $identifierPattern = '(?<![A-Za-z0-9_])(?:' +
        ((@($Policy.LegacyIdentifiers) | ForEach-Object { [regex]::Escape([string]$_) }) -join '|') +
        ')(?![A-Za-z0-9_])'
    for ($lineIndex = 0; $lineIndex -lt $codeLines.Count; ++$lineIndex) {
        foreach ($match in [regex]::Matches(
                [string]$codeLines[$lineIndex],
                $identifierPattern,
                [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
            [void]$legacyIdentifiers.Add([PSCustomObject]([ordered]@{
                line = $lineIndex + 1
                identifier = $match.Value
            }))
        }
    }

    return [PSCustomObject]([ordered]@{
        path = $normalizedPath
        owner = $classification.owner
        owner_kind = $classification.kind
        package = $classification.package
        includes = @($includes)
        legacy_paths = @($legacyPaths)
        legacy_identifiers = @($legacyIdentifiers)
    })
}

function Remove-CMakeComments {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)

    $builder = [System.Text.StringBuilder]::new()
    $quoted = $false
    $escaped = $false
    $lineComment = $false
    $blockCommentEnd = $null
    $bracketArgumentEnd = $null
    for ($index = 0; $index -lt $Text.Length; ++$index) {
        $character = [char]$Text[$index]
        if ($null -ne $blockCommentEnd) {
            if ($Text.Substring($index).StartsWith($blockCommentEnd, [System.StringComparison]::Ordinal)) {
                [void]$builder.Append((' ' * $blockCommentEnd.Length))
                $index += $blockCommentEnd.Length - 1
                $blockCommentEnd = $null
            }
            elseif ($character -eq "`r" -or $character -eq "`n") {
                [void]$builder.Append($character)
            }
            else {
                [void]$builder.Append(' ')
            }
            continue
        }
        if ($null -ne $bracketArgumentEnd) {
            if ($Text.Substring($index).StartsWith($bracketArgumentEnd, [System.StringComparison]::Ordinal)) {
                [void]$builder.Append($bracketArgumentEnd)
                $index += $bracketArgumentEnd.Length - 1
                $bracketArgumentEnd = $null
            }
            else {
                [void]$builder.Append($character)
            }
            continue
        }
        if ($lineComment) {
            if ($character -eq "`r" -or $character -eq "`n") {
                [void]$builder.Append($character)
                $lineComment = $false
            }
            else {
                [void]$builder.Append(' ')
            }
            continue
        }
        if ($quoted) {
            [void]$builder.Append($character)
            if ($escaped) {
                $escaped = $false
            }
            elseif ($character -eq '\') {
                $escaped = $true
            }
            elseif ($character -eq '"') {
                $quoted = $false
            }
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append($character)
            $quoted = $true
            continue
        }
        if ($character -eq '#') {
            $blockMatch = [regex]::Match($Text.Substring($index), '^#\[(?<equals>=*)\[')
            if ($blockMatch.Success) {
                [void]$builder.Append((' ' * $blockMatch.Length))
                $index += $blockMatch.Length - 1
                $blockCommentEnd = ']' + $blockMatch.Groups['equals'].Value + ']'
                continue
            }
            [void]$builder.Append(' ')
            $lineComment = $true
            continue
        }
        if ($character -eq '[') {
            $argumentMatch = [regex]::Match($Text.Substring($index), '^\[(?<equals>=*)\[')
            if ($argumentMatch.Success) {
                [void]$builder.Append($argumentMatch.Value)
                $index += $argumentMatch.Length - 1
                $bracketArgumentEnd = ']' + $argumentMatch.Groups['equals'].Value + ']'
                continue
            }
        }
        [void]$builder.Append($character)
    }
    return $builder.ToString()
}

function New-CMakeInventoryRecord {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
    )

    $legacyReferences = [System.Collections.Generic.List[object]]::new()
    $lines = @([regex]::Split((Remove-CMakeComments -Text $Content), "\r\n|\n|\r"))
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; ++$lineIndex) {
        $normalizedLine = ([string]$lines[$lineIndex]).Replace('\', '/')
        if ($normalizedLine -match '(?i)(?:^|[^A-Za-z0-9_])reference/legacy(?:/|[^A-Za-z0-9_]|$)' -or
            $normalizedLine -match '(?i)(?:^|[^A-Za-z0-9_])legacy-source\.zip(?:[^A-Za-z0-9_]|$)') {
            [void]$legacyReferences.Add([PSCustomObject]([ordered]@{
                line = $lineIndex + 1
                text = ([string]$lines[$lineIndex]).Trim()
            }))
        }
    }
    return [PSCustomObject]([ordered]@{
        path = ConvertTo-SourceBoundaryPath -Path $Path
        legacy_references = @($legacyReferences)
    })
}

function Get-SourceBoundaryInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)]$Policy
    )

    $sourceExtensions = @('.h', '.hh', '.hpp', '.hxx', '.c', '.cc', '.cpp', '.cxx', '.ipp', '.inl', '.tpp')
    $productionRoots = @('apps', 'framework', 'adapters', 'packages', 'user')
    $sourceFiles = [System.Collections.Generic.List[object]]::new()
    $cmakeFiles = [System.Collections.Generic.List[object]]::new()
    $seenSource = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $seenCMake = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($relativeRoot in $productionRoots) {
        $absoluteRoot = Join-Path $RepoRoot $relativeRoot
        if (-not (Test-Path -LiteralPath $absoluteRoot -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File) {
            if ($file.Extension.ToLowerInvariant() -in $sourceExtensions -and $seenSource.Add($file.FullName)) {
                $relativePath = Get-RepositoryRelativePath -RepoRoot $RepoRoot -FullPath $file.FullName
                [void]$sourceFiles.Add((New-SourceInventoryRecord `
                        -Path $relativePath `
                        -Content (Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8) `
                        -RepoRoot $RepoRoot `
                        -Policy $Policy))
            }
            if (($file.Name -eq 'CMakeLists.txt' -or $file.Extension -ieq '.cmake') -and $seenCMake.Add($file.FullName)) {
                $relativePath = Get-RepositoryRelativePath -RepoRoot $RepoRoot -FullPath $file.FullName
                [void]$cmakeFiles.Add((New-CMakeInventoryRecord `
                        -Path $relativePath `
                        -Content (Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8)))
            }
        }
    }

    $rootCMake = Join-Path $RepoRoot 'CMakeLists.txt'
    if ((Test-Path -LiteralPath $rootCMake -PathType Leaf) -and $seenCMake.Add($rootCMake)) {
        [void]$cmakeFiles.Add((New-CMakeInventoryRecord `
                -Path 'CMakeLists.txt' `
                -Content (Get-Content -LiteralPath $rootCMake -Raw -Encoding utf8)))
    }
    $cmakeRoot = Join-Path $RepoRoot 'cmake'
    if (Test-Path -LiteralPath $cmakeRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $cmakeRoot -Recurse -File) {
            if (($file.Name -ne 'CMakeLists.txt' -and $file.Extension -ine '.cmake') -or -not $seenCMake.Add($file.FullName)) {
                continue
            }
            $relativePath = Get-RepositoryRelativePath -RepoRoot $RepoRoot -FullPath $file.FullName
            [void]$cmakeFiles.Add((New-CMakeInventoryRecord `
                    -Path $relativePath `
                    -Content (Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8)))
        }
    }

    return [PSCustomObject]([ordered]@{
        SourceFiles = @($sourceFiles | Sort-Object path)
        CMakeFiles = @($cmakeFiles | Sort-Object path)
    })
}

function Test-SourceBoundaryInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]$Inventory,
        [Parameter(Mandatory = $true)]$Policy
    )

    $findings = [System.Collections.Generic.List[object]]::new()
    foreach ($finding in @(Get-PolicyOwnershipFindings -Policy $Policy)) { [void]$findings.Add($finding) }
    $moduleSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($moduleName in @($Policy.ModuleNames)) { [void]$moduleSet.Add([string]$moduleName) }
    $includeCount = 0

    foreach ($source in @($Inventory.SourceFiles)) {
        $owner = if ($null -eq $source.owner) { $null } else { [string]$source.owner }
        if ([string]::IsNullOrWhiteSpace($owner)) {
            [void]$findings.Add((New-SourceBoundaryFinding `
                    -RuleId 'SB-SOURCE-OWNER' `
                    -Path ([string]$source.path) `
                    -Message 'Production C/C++ source has no registered or logical owner.'))
        }

        foreach ($legacyPath in @($source.legacy_paths)) {
            [void]$findings.Add((New-SourceBoundaryFinding `
                    -RuleId 'SB-LEGACY-PATH' `
                    -Path ([string]$source.path) `
                    -Line ([int]$legacyPath.line) `
                    -Message 'Production source references the frozen Legacy tree or archive.'))
        }
        foreach ($identifier in @($source.legacy_identifiers)) {
            [void]$findings.Add((New-SourceBoundaryFinding `
                    -RuleId 'SB-LEGACY-API' `
                    -Path ([string]$source.path) `
                    -Line ([int]$identifier.line) `
                    -Message "Production source uses retired Legacy API '$($identifier.identifier)'."))
        }

        foreach ($include in @($source.includes)) {
            ++$includeCount
            if (-not [string]::IsNullOrWhiteSpace([string]$include.legacy_identifier)) {
                [void]$findings.Add((New-SourceBoundaryFinding `
                        -RuleId 'SB-LEGACY-API' `
                        -Path ([string]$source.path) `
                        -Line ([int]$include.line) `
                        -Message "Production include uses retired Legacy API '$($include.legacy_identifier)'."))
            }
            if ([string]$include.target_kind -ceq 'unknown-internal') {
                [void]$findings.Add((New-SourceBoundaryFinding `
                        -RuleId 'SB-UNKNOWN-INTERNAL-INCLUDE' `
                        -Path ([string]$source.path) `
                        -Line ([int]$include.line) `
                        -Message "Internal include '$($include.text)' has no ADR-0003 module owner."))
                continue
            }
            if ([string]$include.target_kind -ceq 'unowned-production') {
                [void]$findings.Add((New-SourceBoundaryFinding `
                        -RuleId 'SB-SOURCE-OWNER' `
                        -Path ([string]$source.path) `
                        -Line ([int]$include.line) `
                        -Message "Include '$($include.text)' resolves to production source without an owner."))
                continue
            }
            if ([string]$include.target_kind -notin @('module', 'logical') -or [string]::IsNullOrWhiteSpace($owner)) {
                continue
            }

            $targetOwner = [string]$include.target_owner
            if ($moduleSet.Contains($owner)) {
                if ($targetOwner -in @('packages', 'user', 'apps')) {
                    [void]$findings.Add((New-SourceBoundaryFinding `
                            -RuleId 'SB-PROJECT-REVERSE-INCLUDE' `
                            -Path ([string]$source.path) `
                            -Line ([int]$include.line) `
                            -Message "Framework module '$owner' includes project-owned '$targetOwner' source."))
                    continue
                }
                if ($moduleSet.Contains($targetOwner) -and
                    $targetOwner -notin @($Policy.AllowedModulesByOwner[$owner])) {
                    [void]$findings.Add((New-SourceBoundaryFinding `
                            -RuleId 'SB-DEPENDENCY-DIRECTION' `
                            -Path ([string]$source.path) `
                            -Line ([int]$include.line) `
                            -Message "Source dependency '$owner -> $targetOwner' is outside the approved source seam."))
                }
                continue
            }

            if ($owner -eq 'packages') {
                if ($moduleSet.Contains($targetOwner) -and
                    $targetOwner -notin @($Policy.AllowedModulesByOwner['packages'])) {
                    [void]$findings.Add((New-SourceBoundaryFinding `
                            -RuleId 'SB-DEPENDENCY-DIRECTION' `
                            -Path ([string]$source.path) `
                            -Line ([int]$include.line) `
                            -Message "Package source depends on forbidden module '$targetOwner'."))
                }
                elseif ($targetOwner -eq 'packages' -and
                    ([string]::IsNullOrWhiteSpace([string]$source.package) -or
                     [string]::IsNullOrWhiteSpace([string]$include.target_package) -or
                     [string]$source.package -ine [string]$include.target_package)) {
                    [void]$findings.Add((New-SourceBoundaryFinding `
                            -RuleId 'SB-PACKAGE-CROSS-INCLUDE' `
                            -Path ([string]$source.path) `
                            -Line ([int]$include.line) `
                            -Message 'A package includes another package private source path.'))
                }
                elseif ($targetOwner -in @('user', 'apps')) {
                    [void]$findings.Add((New-SourceBoundaryFinding `
                            -RuleId 'SB-DEPENDENCY-DIRECTION' `
                            -Path ([string]$source.path) `
                            -Line ([int]$include.line) `
                            -Message "Package source depends on project-owned '$targetOwner' source."))
                }
                continue
            }

            if ($owner -eq 'apps') {
                if (($moduleSet.Contains($targetOwner) -and
                     $targetOwner -notin @($Policy.AllowedModulesByOwner['apps'])) -or
                    $targetOwner -in @('packages', 'user')) {
                    [void]$findings.Add((New-SourceBoundaryFinding `
                            -RuleId 'SB-DEPENDENCY-DIRECTION' `
                            -Path ([string]$source.path) `
                            -Line ([int]$include.line) `
                            -Message "Application entry source depends directly on forbidden owner '$targetOwner'."))
                }
            }
        }
    }

    foreach ($cmakeFile in @($Inventory.CMakeFiles)) {
        foreach ($reference in @($cmakeFile.legacy_references)) {
            [void]$findings.Add((New-SourceBoundaryFinding `
                    -RuleId 'SB-LEGACY-CMAKE' `
                    -Path ([string]$cmakeFile.path) `
                    -Line ([int]$reference.line) `
                    -Message 'Production CMake references the frozen Legacy tree or archive.'))
        }
    }

    return [PSCustomObject]([ordered]@{
        IsValid = $findings.Count -eq 0
        SourceFileCount = @($Inventory.SourceFiles).Count
        IncludeCount = $includeCount
        CMakeFileCount = @($Inventory.CMakeFiles).Count
        Findings = @($findings)
    })
}

function New-SyntheticSourceBoundaryInventory {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Sources,
        [object[]]$CMakeFiles = @(),
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)]$Policy
    )

    return [PSCustomObject]([ordered]@{
        SourceFiles = @($Sources | ForEach-Object {
            New-SourceInventoryRecord `
                -Path ([string]$_.path) `
                -Content ([string]$_.content) `
                -RepoRoot $RepoRoot `
                -Policy $Policy
        })
        CMakeFiles = @($CMakeFiles | ForEach-Object {
            New-CMakeInventoryRecord -Path ([string]$_.path) -Content ([string]$_.content)
        })
    })
}

function Invoke-SourceBoundaryNegativeCases {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)]$Policy
    )

    $failures = [System.Collections.Generic.List[string]]::new()
    $positiveSources = @(
        [PSCustomObject]@{
            path = 'framework/include/gnc/kernel/positive.hpp'
            content = "#include <gnc/contracts/state.hpp>`n// SimulationNode`nconstexpr auto text = `"NodeFactory`";`n"
        },
        [PSCustomObject]@{
            path = 'framework/include/gnc/compiler/positive.hpp'
            content = "#include <gnc/model_sdk/model.hpp>`n"
        },
        [PSCustomObject]@{
            path = 'packages/example/model.hpp'
            content = "#include <gnc/model_sdk/model.hpp>`n"
        },
        [PSCustomObject]@{
            path = 'adapters/positive.hpp'
            content = "#include <gnc/application/api.hpp>`n"
        },
        [PSCustomObject]@{
            path = 'apps/cli/positive.cpp'
            content = "#include <gnc/foundation/version.hpp>`n"
        }
    )
    $positiveCMake = @([PSCustomObject]@{
        path = 'cmake/Positive.cmake'
        content = "# add_subdirectory(reference/legacy)`n#[[`nadd_subdirectory(reference/legacy)`n]]`nset(label `"Legacy qualification text`")`n"
    })
    $positiveInventory = New-SyntheticSourceBoundaryInventory `
        -Sources $positiveSources `
        -CMakeFiles $positiveCMake `
        -RepoRoot $RepoRoot `
        -Policy $Policy
    $positiveResult = Test-SourceBoundaryInventory -Inventory $positiveInventory -Policy $Policy
    if (-not $positiveResult.IsValid) {
        [void]$failures.Add("Positive source-boundary control failed: $(@($positiveResult.Findings | ForEach-Object { $_.rule_id }) -join ', ').")
    }

    $cases = [System.Collections.Generic.List[object]]::new()

    $duplicateOwnerPolicy = $Policy.PSObject.Copy()
    $duplicateOwnerPolicy.SourceRoots = @($Policy.SourceRoots) + @(
        [PSCustomObject]@{ owner = 'duplicate_foundation_owner'; root = 'framework/include/gnc/foundation' }
    )
    [void]$cases.Add([PSCustomObject]@{
        name = 'duplicate-source-owner'
        policy = $duplicateOwnerPolicy
        inventory = $positiveInventory
        expected_rules = @('SB-OWNER-UNIQUE')
    })

    foreach ($case in @(
        [PSCustomObject]@{
            name = 'kernel-includes-compiler'
            path = 'framework/include/gnc/kernel/bad.hpp'
            content = "#include <gnc/kernel/../compiler/plan.hpp>`n"
            expected_rules = @('SB-DEPENDENCY-DIRECTION')
        },
        [PSCustomObject]@{
            name = 'adapter-includes-kernel-internal'
            path = 'adapters/bad.hpp'
            content = "#include <gnc/kernel/session.hpp>`n"
            expected_rules = @('SB-DEPENDENCY-DIRECTION')
        },
        [PSCustomObject]@{
            name = 'package-includes-compiler'
            path = 'packages/example/bad.hpp'
            content = "#include <gnc/compiler/compiler.hpp>`n"
            expected_rules = @('SB-DEPENDENCY-DIRECTION')
        },
        [PSCustomObject]@{
            name = 'framework-includes-user-project'
            path = 'framework/include/gnc/application/bad.hpp'
            content = "#include `"user/project.hpp`"`n"
            expected_rules = @('SB-PROJECT-REVERSE-INCLUDE')
        },
        [PSCustomObject]@{
            name = 'unknown-internal-module'
            path = 'framework/include/gnc/application/bad.hpp'
            content = "#include <gnc/unknown_runtime/api.hpp>`n"
            expected_rules = @('SB-UNKNOWN-INTERNAL-INCLUDE')
        },
        [PSCustomObject]@{
            name = 'legacy-source-and-api'
            path = 'framework/include/gnc/kernel/bad.hpp'
            content = "#include `"reference/legacy/SimulationNode.hpp`"`nSimulationNode* node;`n"
            expected_rules = @('SB-LEGACY-PATH', 'SB-LEGACY-API')
        }
    )) {
        $inventory = New-SyntheticSourceBoundaryInventory `
            -Sources @([PSCustomObject]@{ path = $case.path; content = $case.content }) `
            -RepoRoot $RepoRoot `
            -Policy $Policy
        [void]$cases.Add([PSCustomObject]@{
            name = $case.name
            policy = $Policy
            inventory = $inventory
            expected_rules = @($case.expected_rules)
        })
    }

    $legacyCMakeInventory = New-SyntheticSourceBoundaryInventory `
        -Sources @() `
        -CMakeFiles @([PSCustomObject]@{
            path = 'CMakeLists.txt'
            content = "add_subdirectory([[reference/legacy]])`n"
        }) `
        -RepoRoot $RepoRoot `
        -Policy $Policy
    [void]$cases.Add([PSCustomObject]@{
        name = 'legacy-cmake-entry'
        policy = $Policy
        inventory = $legacyCMakeInventory
        expected_rules = @('SB-LEGACY-CMAKE')
    })

    foreach ($case in $cases) {
        $result = Test-SourceBoundaryInventory -Inventory $case.inventory -Policy $case.policy
        if ($result.IsValid) {
            [void]$failures.Add("Negative case '$($case.name)' was accepted.")
            continue
        }
        $actualRules = @($result.Findings | ForEach-Object { [string]$_.rule_id } | Sort-Object -Unique)
        foreach ($expectedRule in @($case.expected_rules)) {
            if ($expectedRule -notin $actualRules) {
                [void]$failures.Add("Negative case '$($case.name)' missed expected rule '$expectedRule'.")
            }
        }
    }

    return [PSCustomObject]([ordered]@{
        IsValid = $failures.Count -eq 0
        CaseCount = $cases.Count
        Failures = @($failures)
    })
}

Export-ModuleMember -Function @(
    'New-SourceBoundaryPolicy',
    'Get-SourceBoundaryInventory',
    'Test-SourceBoundaryInventory',
    'Invoke-SourceBoundaryNegativeCases'
)
