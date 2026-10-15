param(
    [string]$Jdk = $env:JAVA_HOME,
    [string]$ForgeJar
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$Jdk -or !(Test-Path -LiteralPath "$Jdk\bin\javac.exe")) { throw 'Pass -Jdk with a JDK 9+ installation path.' }
if (!$ForgeJar -or !(Test-Path -LiteralPath $ForgeJar)) { throw 'Pass -ForgeJar with the official Forge 1.8.9-11.15.1.2318-1.8.9 universal JAR.' }
if ((Get-FileHash -LiteralPath $ForgeJar -Algorithm SHA1).Hash -ne 'beda619c465af293e63952dd573c137c17c0a4cd') { throw 'Forge reference checksum mismatch.' }
$native = Join-Path $repo 'build\Release\jni_probe.dll'
if (!(Test-Path -LiteralPath $native)) { throw 'Build Release with LUNAR_LAB_JNI_PROBE=ON first.' }
# A fresh staging directory avoids stale classes without recursive deletion.
$stage = Join-Path $repo ('build\forge-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$stage\native", "$repo\dist" | Out-Null
$sources = @("$repo\jni\lab\Probe.java", "$repo\forge\src\lab\NativeBootstrap.java", "$repo\forge\src\lab\LunarLocalDiagnostic.java")
& "$Jdk\bin\javac.exe" --release 8 -classpath $ForgeJar -d $stage @sources
if ($LASTEXITCODE -ne 0) { throw 'Forge diagnostic compilation failed.' }
Copy-Item -LiteralPath $native -Destination "$stage\native\jni_probe.dll"
$hash = (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$stage\native\library.properties" -Value "sha256=$hash" -Encoding ascii
Copy-Item -LiteralPath "$repo\forge\mcmod.info" -Destination "$stage\mcmod.info"
$artifact = Join-Path $repo 'dist\lunar-local-diagnostic-0.2.0.jar'
& "$Jdk\bin\jar.exe" cf $artifact -C $stage .
if ($LASTEXITCODE -ne 0) { throw 'JAR packaging failed.' }
Write-Output "Created diagnostic-only artifact: $artifact"
Write-Output "SHA256: $((Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash)"
