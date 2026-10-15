param([Parameter(Mandatory)][string]$Java)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$jar = Join-Path $repo 'dist\lunar-local-diagnostic-0.2.0.jar'
$destination = Join-Path $repo ('build\packaged-verification-' + [guid]::NewGuid().ToString('N'))
& $Java -Xcheck:jni -cp $jar lab.NativeBootstrap $destination
if ($LASTEXITCODE -ne 0) { throw 'Packaged diagnostic failed.' }
# A second fresh JVM checks existing DLL reuse and checksum verification.
& $Java -Xcheck:jni -cp $jar lab.NativeBootstrap $destination
if ($LASTEXITCODE -ne 0) { throw 'Packaged diagnostic reuse failed.' }
$dll = @(Get-ChildItem -LiteralPath "$destination\native" -Recurse -File -Filter jni_probe.dll)
if ($dll.Count -ne 1) { throw 'Expected exactly one extracted test DLL.' }
# Only corrupt the newly generated test copy inside build/, never an installation.
[IO.File]::WriteAllBytes($dll[0].FullName, [byte[]]@(0,1,2,3))
$output = & $Java -Xcheck:jni -cp $jar lab.NativeBootstrap $destination 2>&1
if ($LASTEXITCODE -eq 0 -or ($output -join "`n") -notmatch 'Existing DLL checksum mismatch') {
    throw 'Expected integrity rejection was not observed.'
}
Write-Output 'Packaged tests passed: first load, existing library reuse, rejection of corrupted test DLL.'
$global:LASTEXITCODE = 0 # The last native failure above is the expected negative test.
