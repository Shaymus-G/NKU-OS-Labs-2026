# Render actual log excerpts; these images are NOT terminal screenshots.
Add-Type -AssemblyName System.Drawing
$repoRoot = Split-Path -Parent $PSScriptRoot

function Render-LogExcerpt([string]$logName, [string]$imageName, [string]$pattern) {
    $logPath = Join-Path $repoRoot "docs/logs/$logName"
    $lines = @(Get-Content -LiteralPath $logPath | Where-Object { $_ -match $pattern })
    $displayLines = @(
        'LAB2 / MEMBER C / 2412456',
        'Rendered excerpt from the actual log (not a terminal screenshot)',
        "Source: docs/logs/$logName",
        ''
    ) + $lines
    $bitmap = [System.Drawing.Bitmap]::new(1500, 80 + 34 * $displayLines.Count)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $font = [System.Drawing.Font]::new('Consolas', 19)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(26, 41, 58))
    try {
        $graphics.Clear([System.Drawing.Color]::White)
        $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        for ($index = 0; $index -lt $displayLines.Count; $index++) {
            $graphics.DrawString($displayLines[$index], $font, $brush, 30, 30 + 34 * $index)
        }
        $imagePath = Join-Path $repoRoot "docs/images/$imageName"
        $bitmap.Save($imagePath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $brush.Dispose()
        $font.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

Render-LogExcerpt 'c-buddy-run.log' 'c-buddy-log.png' '^HEAD:|^memory management:|^  free range:|^\[Buddy\]|^check_alloc_page'
Render-LogExcerpt 'c-grade.log' 'c-grade-log.png' '^HEAD:|^\$ make grade|check page init:|check best fit:|^Total Score:'
