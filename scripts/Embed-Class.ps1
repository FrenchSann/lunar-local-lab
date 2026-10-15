param([Parameter(Mandatory)][string]$InputClass,[Parameter(Mandatory)][string]$OutputHeader)
$ErrorActionPreference='Stop'
$bytes=[IO.File]::ReadAllBytes($InputClass)
$values=($bytes | ForEach-Object { '0x{0:X2}' -f $_ }) -join ','
[IO.File]::WriteAllText($OutputHeader,"#pragma once`nstatic const unsigned char gameBridgeBytes[] = {$values};`n")
