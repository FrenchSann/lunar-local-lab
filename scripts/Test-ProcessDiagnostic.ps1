param([string]$Jdk = $env:JAVA_HOME)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$Jdk) { throw 'JDK required.' }
& "$Jdk\bin\javac.exe" --release 8 -d "$repo\build\java" "$repo\jni\lab\ProbeHost.java"
if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed.' }
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = "$Jdk\bin\java.exe"
$info.ArgumentList.Add('-cp')
$info.ArgumentList.Add("$repo\build\java")
$info.ArgumentList.Add('lab.ProbeHost')
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$hostProcess = [Diagnostics.Process]::Start($info)
try {
    $ready = $hostProcess.StandardOutput.ReadLineAsync()
    if (!$ready.Wait(10000) -or $ready.Result -ne 'Diagnostic host ready') { throw 'Owned test JVM did not become ready.' }
    $inspector = Join-Path $repo 'build\Release\inspect_process.exe'
    & $inspector $hostProcess.Id --read-header
    if ($LASTEXITCODE -ne 0) { throw 'External read failed.' }
    & $inspector $hostProcess.Id --inject-diagnostic
    if ($LASTEXITCODE -ne 0) { throw 'Injection diagnostic failed.' }
    & $inspector $hostProcess.Id --inject-diagnostic
    if ($LASTEXITCODE -ne 0) { throw 'Second injection after verified unload failed.' }
    $rejected = & $inspector $PID --read-header 2>&1
    if ($LASTEXITCODE -eq 0 -or ($rejected -join "`n") -notmatch 'Target is not a Java executable') { throw 'Non-Java target was not rejected.' }
    Write-Output 'Process diagnostic tests passed: external read, two load/probe/unload cycles, non-Java rejection.'
    $global:LASTEXITCODE = 0
} finally {
    # Only the test process created above, never the user's Lunar instance.
    if (!$hostProcess.HasExited) { $hostProcess.Kill(); $hostProcess.WaitForExit() }
    $hostProcess.Dispose()
}
