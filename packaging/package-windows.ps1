param(
    [Parameter(Mandatory = $true)][string]$Stage,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Add-Type -AssemblyName System.IO.Compression
$root = (Get-Item -LiteralPath $Stage).FullName.TrimEnd('\') + '\'
$target = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination)
if ($target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ZIP destination must be outside the runtime stage.'
}
$files = @(Get-ChildItem -LiteralPath $Stage -Recurse -Force)
if (@($files | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
    throw 'Runtime stage contains a reparse point.'
}
$files = @($files | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName)
if (-not $files.Count) { throw 'Runtime stage is empty.' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
$temp = $target + '.tmp'
$archive = $null
$stream = $null
try {
    $stream = [IO.File]::Open($temp, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length).Replace('\', '/')
        if (-not $relative -or $relative.StartsWith('/') -or $relative.Split('/') -contains '..') {
            throw 'Non-canonical archive entry.'
        }
        $entry = $archive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]'1980-01-01T00:00:00+00:00'
        $inputStream = $null
        $outputStream = $null
        try {
            $inputStream = $file.OpenRead()
            $outputStream = $entry.Open()
            $inputStream.CopyTo($outputStream)
        } finally {
            if ($outputStream) { $outputStream.Dispose() }
            if ($inputStream) { $inputStream.Dispose() }
        }
    }
    $archive.Dispose(); $archive = $null
    $stream.Dispose(); $stream = $null
    Move-Item -LiteralPath $temp -Destination $target -Force
    Write-Output ('Packaged {0} runtime files.' -f $files.Count)
} finally {
    if ($archive) { $archive.Dispose() }
    if ($stream) { $stream.Dispose() }
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
}
