<#
.SYNOPSIS
  Firma con Authenticode los .exe y .dll de una carpeta (o un archivo) con marca de tiempo (Fase 19, D3).

.DESCRIPTION
  Usa signtool.exe (Windows SDK) con un certificado en un archivo .pfx. La clave NO vive en el repositorio: en GitHub Actions
  llega por secretos (WINDOWS_CERT_PFX_BASE64 y WINDOWS_CERT_PASSWORD) y en tu PC por variables de entorno.
  Si no hay certificado configurado, no firma y sale sin error (así el mismo flujo sirve para compilaciones sin firmar).
  Para firma en la nube (Azure Trusted Signing, SignPath) ver docs/distribucion.md: se hace en el flujo, no con este script.

.PARAMETER Ruta
  Carpeta (se firman todos sus .exe y .dll) o archivo.

.EXAMPLE
  $env:WINDOWS_CERT_PFX = "C:\claves\nufiocad.pfx"; $env:WINDOWS_CERT_PASSWORD = "..."
  .\packaging\windows\firmar.ps1 -Ruta .\build\windows-release\_CPack_Packages\win64\NSIS
#>
param(
    [Parameter(Mandatory = $true)][string]$Ruta
)

$ErrorActionPreference = "Stop"

$pfx = $env:WINDOWS_CERT_PFX
if ([string]::IsNullOrWhiteSpace($pfx) -or -not (Test-Path $pfx)) {
    Write-Host "firmar.ps1: no hay certificado (WINDOWS_CERT_PFX); se deja sin firmar."
    exit 0
}

# signtool: el del Windows SDK más nuevo que haya.
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { throw "No se encontró signtool.exe (instala el Windows SDK)." }

$archivos = if (Test-Path $Ruta -PathType Container) {
    Get-ChildItem $Ruta -Recurse -Include *.exe, *.dll | Select-Object -ExpandProperty FullName
} else {
    @((Resolve-Path $Ruta).Path)
}
if (-not $archivos) { Write-Host "firmar.ps1: no hay nada que firmar en $Ruta"; exit 0 }

$marcaDeTiempo = if ($env:WINDOWS_CERT_TIMESTAMP_URL) { $env:WINDOWS_CERT_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }

foreach ($archivo in $archivos) {
    # Lo que ya está firmado (las DLL de Qt vienen firmadas por The Qt Company) se deja como está.
    $firma = Get-AuthenticodeSignature $archivo
    if ($firma.Status -eq "Valid") { continue }
    & $signtool sign /fd SHA256 /tr $marcaDeTiempo /td SHA256 /f $pfx /p $env:WINDOWS_CERT_PASSWORD $archivo | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "No se pudo firmar $archivo" }
}
Write-Host "firmar.ps1: firmados $($archivos.Count) archivos (los ya firmados se respetaron)."
