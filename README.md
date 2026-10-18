# NufioCAD — CAD paramétrico de escritorio

CAD paramétrico **offline**, con archivos locales privados, para **Windows y Linux**.
Meta de usabilidad: Fusion 360. Kernel: OpenCASCADE. Interfaz: Qt 6.

> **NufioCAD** es el nombre de trabajo. En el código se usa el prefijo corto
> `nuf` (namespaces `nuf::`, targets `nuf::<módulo>`), igual que la extensión
> de los archivos de diseño (`.nuf`).

Documentación de diseño en [`docs/`](docs/):
[hoja de ruta](docs/hoja-de-ruta-cad.md) · [arquitectura](docs/arquitectura-cad.md).

---

## Estado

Fase 0 (fundamentos). La app abre una ventana con la distribución del mockup y
valida el stack completo: OpenCASCADE construye un sólido, SQLite crea y reabre
un `.nuf`, y deshacer/rehacer funciona con el primer comando real.

---

## Estructura

```
NufioCAD/
├── CMakeLists.txt         proyecto raíz
├── CMakePresets.json      un preset por sistema (Linux / Windows)
├── vcpkg.json             dependencias con versión fijada (OCCT, SQLite, Catch2)
├── cmake/                 funciones comunes (advertencias, bibliotecas)
├── src/
│   ├── geometry/          adaptador de OpenCASCADE (único que lo incluye)
│   ├── sketch/            sketcher 2D (Fase 2)
│   ├── document/          documento, señales, timeline (Fase 3)
│   ├── commands/          deshacer/rehacer
│   ├── io/                archivo .nuf (SQLite) y exportación
│   ├── render/            viewport (Qt + OCCT, Fase 1)
│   └── app/               ventana principal y ejecutable
├── tests/                 pruebas del núcleo (Catch2, sin Qt)
├── docs/                  hoja de ruta y arquitectura
└── .github/workflows/     CI en Windows y Linux
```

Cada módulo expone sus cabeceras en `src/<módulo>/include/nuf/<módulo>/` y se
usa como `nuf::<módulo>` en CMake. El núcleo (`geometry`, `document`,
`commands`, `io`) **no depende de Qt**.

---

## Compilar en Linux (Fedora)

### 1. Instalar dependencias

```bash
sudo dnf install gcc-c++ cmake ninja-build git \
    opencascade-devel qt6-qtbase-devel sqlite-devel catch-devel
```

Si CMake reporta que falta TBB al configurar, instala también `tbb-devel`.

### 2. Configurar, compilar y probar

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

### 3. Ejecutar

```bash
./build/linux-debug/src/app/NufioCAD                # abre la app
./build/linux-debug/src/app/NufioCAD --self-check   # verifica el stack sin ventana
```

> Para usar las mismas versiones fijadas que Windows, existe el preset
> `linux-vcpkg-release` (requiere vcpkg y la variable `VCPKG_ROOT`).

---

## Compilar en Windows

### 1. Instalar herramientas (una sola vez)

1. **Visual Studio 2022** (Community sirve) con la carga de trabajo
   *Desarrollo para el escritorio con C++* (incluye CMake y Ninja).
2. **Git**.
3. **vcpkg**:

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\dev\vcpkg
   ```

4. **Qt 6** con el instalador oficial (Qt Online Installer): componente
   *MSVC 2022 64-bit*. Luego indica dónde quedó:

   ```powershell
   setx QT_ROOT C:\Qt\6.8.3\msvc2022_64
   ```

   (ajusta la ruta a la versión que instalaste).

### 2. Configurar, compilar y probar

Desde **x64 Native Tools Command Prompt for VS 2022**:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

La primera configuración tarda: vcpkg compila OpenCASCADE desde el código
fuente. Las siguientes reutilizan lo compilado.

También puedes abrir la carpeta directamente en Visual Studio: detecta
`CMakePresets.json` y muestra los presets de Windows.

### 3. Ejecutar

```powershell
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
.\build\windows-debug\src\app\NufioCAD.exe
```

---

## Fijar versiones de vcpkg (una sola vez)

Para que todos compilen con las mismas versiones de las dependencias:

```bash
vcpkg x-update-baseline --add-initial-baseline
git commit -am "Fijar baseline de vcpkg"
```

Esto agrega `builtin-baseline` a `vcpkg.json`.

---

## Reglas de código multiplataforma

Resumen de `docs/arquitectura-cad.md`, sección 9.1:

- Rutas siempre con `std::filesystem::path`; nunca armarlas a mano con `/` o `\`.
- Texto en UTF-8 en el núcleo; conversión a `QString` solo en la interfaz.
- Nada de APIs exclusivas de un sistema (`unistd.h`, `windows.h`) en el núcleo.
- Las advertencias son errores en MSVC y GCC/Clang.
- Formato del código con `.clang-format`.
