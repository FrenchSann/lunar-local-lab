param([string]$LunarRoot = (Join-Path $env:USERPROFILE '.lunarclient'))
$ErrorActionPreference = 'Stop'
$launcher = Join-Path $env:LOCALAPPDATA 'Programs\Lunar Client\Lunar Client.exe'
$clientJar = Join-Path $LunarRoot 'offline\multiver\lunar.jar'
$profiles = @()
if (Test-Path -LiteralPath "$LunarRoot\profiles") {
    foreach ($file in Get-ChildItem -LiteralPath "$LunarRoot\profiles" -Recurse -File -Filter profile.json) {
        try {
            $p = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            # Deliberately exclude names, IDs, descriptions, tokens and arbitrary JSON.
            $profiles += [pscustomobject]@{
                version = if ($p.gameVersion -match '^\d+\.\d+(\.\d+)?$') { $p.gameVersion } else { 'unknown' }
                loaders = @($p.loaders | Where-Object { $_ -in @('ichor','forge','fabric') })
            }
        } catch { $profiles += [pscustomobject]@{version='unreadable';loaders=@()} }
    }
}
$java = @()
foreach ($process in Get-Process java,javaw -ErrorAction SilentlyContinue) {
    # Never access CommandLine, environment variables or JVM system-property dumps.
    try {
        $java += [pscustomobject]@{ pid=$process.Id; executable=$process.ProcessName; version=$process.MainModule.FileVersionInfo.ProductVersion }
    } catch { $java += [pscustomobject]@{pid=$process.Id;executable=$process.ProcessName;version='unreadable'} }
}
[pscustomobject]@{
    lunarFilesPresent = Test-Path -LiteralPath $LunarRoot
    launcherVersion = if(Test-Path -LiteralPath $launcher){ (Get-Item -LiteralPath $launcher).VersionInfo.ProductVersion } else {'missing'}
    localJarSha1 = if(Test-Path -LiteralPath $clientJar){ (Get-FileHash -LiteralPath $clientJar -Algorithm SHA1).Hash.ToLowerInvariant() } else {'missing'}
    referenceJarSha1 = '56b65a5e24e521d47ea44d07eee75922c9fcc2c0'
    profiles = $profiles
    javaProcesses = $java
    integration = 'unverified; process presence alone does not establish client, loader, build or server context'
} | ConvertTo-Json -Depth 5
