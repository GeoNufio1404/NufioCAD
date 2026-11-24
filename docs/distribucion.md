# Distribución: instalador, firma y publicación

Fase 19 de la [hoja de ruta](hoja-de-ruta.md). Esta guía dice **qué hay hecho**, **cómo se construye** el instalador, **cómo
se firma**, qué esperar de SmartScreen y Defender, y la lista de lo que hay que decidir antes de publicar la v0.2.0.

## Lo que hay (y lo que falta probar)

| Pieza | Estado |
|---|---|
| Reglas de instalación de Windows (`src/app/CMakeLists.txt`): el programa, las DLL de OpenCASCADE y de vcpkg, Qt con `windeployqt` (DLL y plugins), `qt.conf`, avisos de licencias | ✅ **Probado el 19 nov. 2026**: `cmake --install` deja una copia que arranca y pasa la prueba de interfaz (113 MB) |
| Paquete ZIP portátil (`cpack -G ZIP`) | ✅ Probado: `NufioCAD-0.2.0-win64.zip`, 44,7 MB |
| Instalador NSIS (`cpack -G NSIS`): menú Inicio, escritorio, asociación de `.nuf`, desinstalador, avisos de terceros | ✅ **Probado** (24 nov. 2026, NSIS 3.13: `NufioCAD-0.2.0-win64.exe`, 65 MB; las órdenes de asociación de `.nuf` fallaban y se corrigieron). Instalado y probado por Geonufio (24 nov. 2026). El ZIP del mismo paquete, en una carpeta limpia y sin Qt en el PATH, pasa la prueba de interfaz (59 pasos, 0 fallidos) |
| Linux: `.tar.gz`, `.deb`, `.rpm` (`cpack`) y AppImage (`packaging/linux/crear-appimage.sh`) | ⚠️ Configurado, **sin probar**; el CI de Linux ya comprueba que el `.tar.gz` se genera |
| Firma (`packaging/windows/firmar.ps1` y `cmake/FirmarPaquete.cmake`) | ⚠️ Listo, pero **hace falta un certificado** (decisión tuya, ver abajo) |
| Flujo `.github/workflows/publicar.yml` (etiqueta `vX.Y.Z`): compila, empaqueta, firma si hay certificado, SHA-256 y versión en borrador | ⚠️ **Sin ejecutar**: la primera vez, lanzarlo a mano (Actions › Publicar › Run workflow) y mirar el resultado |
| Avisos de licencias de terceros (`packaging/AVISOS-DE-TERCEROS.txt`), instalados junto al programa | ✅ |
| Plantillas de winget (`packaging/windows/winget/`) | ✅ Listas para rellenar |

## Antes de publicar: lo que decides tú

1. ~~**La licencia de NufioCAD.**~~ ✅ 24 nov. 2026: **MIT** (código abierto), archivo `LICENSE` en la raíz; es la página de
   licencia del instalador y se instala junto al programa. Es una licencia aprobada por la OSI, así que cumple lo que pide
   SignPath Foundation para firmar gratis. Las bibliotecas que se usan permiten cualquier opción (LGPL: Qt y OpenCASCADE se enlazan de forma dinámica y PlaneGCS de forma
   estática, por lo que hay que ofrecer su código y permitir reenlazar: ya se dice en los avisos).
2. **El firmante** (ver «Firma»). Sin firma, la versión se puede publicar igual, pero Windows 11 con Smart App Control en
   modo estricto **no la deja ejecutar**.
3. **El nombre del editor** (`CPACK_PACKAGE_VENDOR`, hoy «NufioCAD») y la dirección de contacto.

## Construir el instalador a mano (Windows)

Con la compilación Release hecha (`cmake --preset windows-release` y `cmake --build build/windows-release`, ver
[`desarrollo.md`](desarrollo.md)):

```powershell
# Una copia lista para ejecutar, para probar:
cmake --install build/windows-release --prefix C:\temp\nufiocad
C:\temp\nufiocad\bin\NufioCAD.exe

# El ZIP portátil y el instalador (necesita NSIS instalado: choco install nsis):
cd build/windows-release
cpack -G "NSIS;ZIP"
```

Qué se instala: `bin\` (el programa, `qt.conf`, las DLL de Qt, de OpenCASCADE y de vcpkg), `plugins\` y `translations\` de Qt,
`licencias\` y `AVISOS-DE-TERCEROS.txt`. El instalador crea el acceso del menú Inicio y del escritorio, asocia los archivos
`.nuf` (icono de la aplicación y «Abrir con NufioCAD») y registra el desinstalador, que también limpia la asociación.

Si el programa arranca sin mostrar ventana, falta alguna DLL: mira que `bin\` tenga las de vcpkg (`freetype.dll`, `libpng16.dll`,
`z.dll`, `bz2.dll`, `brotlidec.dll`, `sqlite3.dll`…). Fue el primer fallo de la prueba del 19 nov. 2026.

## Construir en Linux

```bash
cmake --preset linux-release && cmake --build --preset linux-release
cd build/linux-release && cpack           # .tar.gz, .deb y .rpm
cd ../.. && packaging/linux/crear-appimage.sh build/linux-release   # NufioCAD-<versión>-x86_64.AppImage
```

El AppImage lleva Qt dentro (con `linuxdeploy` y su complemento de Qt, que se bajan la primera vez). El `.deb` y el `.rpm` dependen
de los paquetes de Qt y de OpenCASCADE de la distribución. Un Flatpak (con su manifiesto) queda como paso posterior.

## Publicar una versión

1. Todo lo de la versión fusionado en `main`, con la versión puesta en `project(NufioCAD VERSION …)` de `CMakeLists.txt` (de ahí sale la
   del programa, la del instalador y la de los paquetes).
2. `git tag v0.2.0 && git push origin v0.2.0`. El flujo **Publicar** compila Windows y Linux, empaqueta, firma (si hay certificado), calcula
   los SHA-256 y deja la versión **en borrador** en GitHub, con los archivos y los SHA-256 en las notas.
3. Se revisa el borrador (notas, archivos), se instala y prueba el instalador en un Windows limpio y se publica.

## Firma de código (D3)

Qué se firma: **el `.exe`, todas las DLL propias y el instalador** (las DLL de Qt ya vienen firmadas por The Qt Company y se respetan),
con **marca de tiempo** para que la firma siga valiendo cuando caduque el certificado. La clave **no vive en ningún PC ni en el
repositorio**: va en secretos de GitHub.

Opciones (de menos a más trabajo; los precios cambian, miralos antes de decidir):

| Opción | Cómo | Notas |
|---|---|---|
| **SignPath Foundation** | Solicitud gratuita para proyectos de código abierto: [signpath.org](https://signpath.org). La firma la hace su servicio desde el CI | Gratis, pero exige que el proyecto sea abierto y tenga licencia OSI: depende de la decisión de licencia |
| **Azure Trusted Signing** | Cuenta de Azure, identidad verificada, perfil de certificado; acción `azure/trusted-signing-action` en el flujo | Unos 10 USD al mes según país y tipo de cuenta; la reputación de SmartScreen se construye más rápido |
| **Certificado OV/EV en archivo** | Se compra a una autoridad; los certificados nuevos exigen clave en token o en la nube (HSM) | Cientos de dólares al año. Con un `.pfx` exportable el flujo ya funciona tal cual |

**Con un certificado en `.pfx`** (el caso que ya cubre el flujo): en GitHub, Settings › Secrets and variables › Actions:
`WINDOWS_CERT_PFX_BASE64` (el `.pfx` en base64: `[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))`) y
`WINDOWS_CERT_PASSWORD`. Con ellos, **Publicar** firma sola; sin ellos, deja el instalador sin firmar y no falla. En tu PC:

```powershell
$env:WINDOWS_CERT_PFX = "C:\claves\nufiocad.pfx"; $env:WINDOWS_CERT_PASSWORD = "..."
$env:NUF_FIRMAR = "1"; cd build/windows-release; cpack -G NSIS      # firma los binarios del paquete
..\..\packaging\windows\firmar.ps1 -Ruta .\NufioCAD-0.2.0-win64.exe # y luego el instalador
```

Para **SignPath** o **Azure Trusted Signing** se cambia el paso «Firmar» del flujo por la acción de ese servicio (cada uno tiene su guía
y sus secretos); el resto del flujo no cambia.

## SmartScreen y reputación (D4)

La reputación de SmartScreen es **del certificado, no de cada compilación**: por eso conviene publicar siempre con el mismo certificado y
no cambiar de firmante. Aunque el programa esté firmado, es normal ver el aviso de «Windows protegió su PC» en las primeras descargas
hasta que el certificado acumula descargas sin quejas (con un certificado EV o Trusted Signing es más rápido). Las notas de cada versión llevan
los **SHA-256** para que quien lo desee compruebe la descarga (`Get-FileHash NufioCAD-0.2.0-win64.exe`).

## Falsos positivos de antivirus (D5)

Para reducirlos: compilar siempre en el CI público (el flujo **Publicar**), **sin empaquetadores ni ofuscadores**, firmar, y no cambiar
de certificado. Si Defender marca un archivo como amenaza:

1. Guarda el nombre exacto de la detección (Seguridad de Windows › Historial de protección).
2. Envíalo a Microsoft en <https://www.microsoft.com/en-us/wdsi/filesubmission> (gratis; elige «Software developer» y el SHA-256 de la
   descarga). Es la única revisión a la que se puede recurrir: la respuesta tarda de horas a unos días.
3. Mientras tanto, avisa en las notas de la versión con la detección y el SHA-256.

## winget y Microsoft Store (D6, opcional)

**winget:** las plantillas están en `packaging/windows/winget/`. Con la versión publicada, copia los tres archivos (quitando `.plantilla`) a
`manifests/n/NufioCAD/NufioCAD/<versión>/` de un fork de [winget-pkgs](https://github.com/microsoft/winget-pkgs), rellena `<VERSION>`,
`<URL>`, `<SHA256>` y `<LICENCIA>`, valida con `winget validate --manifest <carpeta>` y abre el pull request (sus robots escanean el
instalador). **Microsoft Store (MSIX):** requiere cuenta de desarrollador (pago único pequeño), empaquetar como MSIX y pasar su
certificación; Microsoft firma el paquete. Queda como paso posterior.

## Licencias en el instalador (D7)

`packaging/AVISOS-DE-TERCEROS.txt` lista cada biblioteca con su licencia y dónde obtener su código, y el instalador lo muestra en la
página de licencia. PlaneGCS (LGPL-2.1+) se enlaza estáticamente: se ofrece su código (`third_party/planegcs/`) y el de NufioCAD para volver
a compilar. Al subir de versión una biblioteca o añadir otra, **actualiza este archivo** (y `README.md`).

## Lista antes de publicar la v0.2.0

- [ ] Elegir y añadir la licencia de NufioCAD (`LICENSE`).
- [ ] Elegir el firmante y poner los secretos (o publicar sin firmar, sabiendo lo de Smart App Control).
- [ ] Lanzar **Publicar** a mano una vez y arreglar lo que falle (nunca se ha ejecutado).
- [ ] Probar el instalador NSIS en un Windows limpio: instala, abre un `.nuf` con doble clic, desinstala y comprueba que no queda la asociación.
- [ ] Probar el AppImage y el `.deb` en Fedora y Ubuntu (Fase 14, P3).
- [ ] Subir la versión en `CMakeLists.txt`, fusionar a `main` y poner la etiqueta (Fase 14, P4).
