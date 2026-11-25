# Comprueba que un paquete ya desempaquetado (el ZIP) se basta solo: cada DLL que piden sus ejecutables y bibliotecas está en el
# paquete o es de las que trae Windows. Las de Visual C++ (vcruntime140, msvcp140...) NO cuentan como de Windows aunque estén en
# System32: las pone otro programa, y en un equipo sin ellas NufioCAD no arrancaría (pasó en la primera 0.2.1).
# Uso: packaging\windows\comprobar-dependencias.ps1 -Carpeta <carpeta desempaquetada>      (sale con 1 si falta algo)
param([Parameter(Mandatory = $true)][string]$Carpeta)

$dumpbin = (Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1).FullName
if (-not $dumpbin) { Write-Error 'No se encontro dumpbin.exe (Visual Studio o Build Tools 2022)'; exit 2 }
$files = Get-ChildItem $Carpeta -Recurse -File -Include *.dll, *.exe | Where-Object { $_.Name -ne 'Uninstall.exe' }
if (-not $files) { Write-Error "No hay binarios en $Carpeta"; exit 2 }
$have = @{}
foreach ($f in $files) { $have[$f.Name.ToLower()] = $true }
$missing = @{}
foreach ($f in $files) {
    foreach ($line in (& $dumpbin /nologo /dependents $f.FullName)) {
        if ($line -notmatch '^\s+(\S+\.(dll|drv))\s*$') { continue }
        $name = $Matches[1].ToLower()
        if ($have.ContainsKey($name) -or $name -like 'api-ms-win-*' -or $name -like 'ext-ms-*') { continue }
        $msvc = $name -match '^(vcruntime|msvcp|concrt|vccorlib|vcomp|mfc)\d'
        if ($msvc -or -not (Test-Path (Join-Path $env:SystemRoot "System32\$name"))) {
            if (-not $missing.ContainsKey($name)) { $missing[$name] = @() }
            $missing[$name] += $f.Name
        }
    }
}
if ($missing.Count -gt 0) {
    foreach ($name in ($missing.Keys | Sort-Object)) { "FALTA $name (la piden: $(($missing[$name] | Select-Object -First 4) -join ', '))" }
    exit 1
}
"OK: $($files.Count) binarios; todo lo que piden esta en el paquete o es de Windows"
exit 0
