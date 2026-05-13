$ErrorActionPreference = 'Stop'

$docsDir = $PSScriptRoot
$repoRoot = Split-Path -Parent $docsDir
$doxyfile = Join-Path $docsDir 'Doxyfile'
$outputDir = Join-Path $repoRoot 'build\docs\doxygen'

$doxygenCommand = Get-Command doxygen -ErrorAction SilentlyContinue
if (-not $doxygenCommand) {
    $defaultWindowsPath = 'C:\Program Files\doxygen\bin\doxygen.exe'
    if (Test-Path -LiteralPath $defaultWindowsPath) {
        $doxygenCommand = Get-Item -LiteralPath $defaultWindowsPath
    }
}

if (-not $doxygenCommand) {
    throw 'Doxygen was not found in PATH or C:\Program Files\doxygen\bin\doxygen.exe'
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Push-Location $repoRoot
try {
    & $doxygenCommand.FullName $doxyfile
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}

$htmlDir = Join-Path $outputDir 'html'
if (Test-Path -LiteralPath $htmlDir) {
    $localizedToggle = -join ([char[]](
        0x041F, 0x043E, 0x043A, 0x0430, 0x0437, 0x0430, 0x0442, 0x044C,
        0x0020,
        0x0438, 0x043B, 0x0438,
        0x0020,
        0x0441, 0x043A, 0x0440, 0x044B, 0x0442, 0x044C,
        0x0020,
        0x0433, 0x043B, 0x0430, 0x0432, 0x043D, 0x043E, 0x0435,
        0x0020,
        0x043C, 0x0435, 0x043D, 0x044E
    ))

    $replacements = @{
        'Toggle main menu visibility' = $localizedToggle
    }

    Get-ChildItem -Path $htmlDir -Filter '*.html' -File -Recurse | ForEach-Object {
        $content = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
        $updated = $content
        foreach ($entry in $replacements.GetEnumerator()) {
            $updated = $updated.Replace($entry.Key, $entry.Value)
        }

        if ($updated -ne $content) {
            Set-Content -LiteralPath $_.FullName -Value $updated -NoNewline -Encoding UTF8
        }
    }
}
