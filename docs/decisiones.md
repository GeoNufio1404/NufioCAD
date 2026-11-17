# Decisiones técnicas: qué se usó, por qué y dónde

Registro de las elecciones de diseño de NufioCAD y de las alternativas que se descartaron. Cómo encajan las piezas está
en [`arquitectura.md`](arquitectura.md).

## Por qué existe NufioCAD

| Programa | Lo que no encajaba |
|---|---|
| FreeCAD | Mismo tipo de kernel (OpenCASCADE), pero el flujo es poco intuitivo (bancos de trabajo separados) |
| Onshape | Necesita internet y, en el plan gratuito, los modelos son públicos |
| Fusion 360 | Es la **referencia de usabilidad**, pero es de pago, en la nube y solo para Windows y macOS |

Meta: un CAD paramétrico **offline**, con **archivos locales privados**, para **Windows y Linux**, tan fácil como
Fusion 360 y pensado para piezas de impresión 3D.

## Tecnologías

| Pieza | Elección | Por qué | Dónde se usa |
|---|---|---|---|
| Lenguaje | **C++20** | Lo exigen el kernel y la interfaz; rendimiento | Todo `src/` |
| Kernel geométrico (B-Rep) | **OpenCASCADE (OCCT) 7.6–8.0** | El único kernel B-Rep libre y maduro: extrusión, revolución, booleanas, redondeo, vaciado, barrido, loft, STEP, mallado | `src/geometry/` (y el visor en `src/render/`) |
| Interfaz | **Qt 6 Widgets** | Madura, con paneles acoplables, atajos y menús; OCCT trae ejemplos de su visor dentro de Qt | `src/app/`, `src/render/` |
| Visor 3D | **V3d/AIS de OCCT** dentro de un `QOpenGLWidget` | Selección de caras y aristas, resaltado y ViewCube ya hechos, con la misma geometría del kernel | `src/render/` |
| Solver de restricciones 2D | **PlaneGCS** (el del sketcher de FreeCAD) + **Eigen** | LGPL (permite distribuir NufioCAD sin GPL), detecta restricciones redundantes y en conflicto y cuenta los grados de libertad. `libslvs` (SolveSpace) es GPLv3 | `third_party/planegcs/` (copia con parche documentado), usado detrás de `ISolver` en `src/sketch/` |
| Archivo nativo | **SQLite** (`.nuf`) con parámetros en **JSON** | Guardado atómico, versionado del esquema, binarios (sólidos importados, miniatura) en el mismo archivo; SQLite recomienda este uso | `src/io/` |
| JSON | **nlohmann/json** | Solo cabeceras y empaquetada en vcpkg, Fedora y Ubuntu | `src/io/DocumentFile.cpp` |
| Pruebas | **Catch2 3** | Pruebas del núcleo sin Qt; etiquetas para separar las lentas (`[.bench]`, `[.perf]`) | `tests/` |
| Build | **CMake + CMakePresets + vcpkg** (manifiesto) | Los mismos comandos en Windows y Linux; versiones fijadas con `builtin-baseline` | `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `cmake/NufHelpers.cmake` |
| Iconos | SVG propios y de [Lucide](https://lucide.dev) (ISC) | Se ven nítidos en pantallas HiDPI y se recolorean con el tema | `src/app/resources/icons/` |
| CI | GitHub Actions: Ubuntu 24.04 (paquetes del sistema) y Windows (MSVC + vcpkg) | Un cambio que rompe un sistema se ve enseguida | `.github/workflows/ci.yml` |

## Decisiones de diseño

### Documento propio con comandos (y no OCAF)

| Opción | A favor | En contra |
|---|---|---|
| OCAF (el marco de documentos de OCCT) | Trae deshacer, recálculo por dependencias, nombrado (TNaming) y persistencia | Árbol de etiquetas diseñado a mano; no se pueden borrar etiquetas con el documento abierto; ata todo el modelo al kernel |
| **Modelo propio + comandos** ✅ | Simple, se prueba sin kernel ni ventana, control total | Hay que resolver el nombrado topológico por cuenta propia |

FreeCAD tampoco usa OCAF para su documento. Detalle en [`arquitectura.md`](arquitectura.md) §3.

### Nombrado topológico con un resolvedor propio

En la Fase 0 se dejó abierta la puerta a usar TNaming de OCCT. **En la Fase 3 se decidió un resolvedor propio:**
TNaming necesita el árbol de etiquetas de OCAF (descartado). Cada operación nombra sus caras por su origen y las
booleanas propagan los nombres con el historial de OCCT; si un nombre se repite, se desempata por posición. Es el mismo
enfoque que FreeCAD 1.0 y Onshape. Ver [`arquitectura.md`](arquitectura.md) §6.

### Deshacer con memento de parámetros

| Opción | A favor | En contra |
|---|---|---|
| Lógica inversa escrita a mano en cada comando | Poca memoria | Cada comando, una fuente de errores |
| Copia completa del documento en cada paso | Muy robusto | Desperdicia memoria con sólidos importados |
| **Copia de la operación antes y después** ✅ | Robusto y barato (los parámetros pesan KB) | Cada operación debe saber clonarse (se necesita igual para guardar) |

### Qt solo en la interfaz

| Opción | A favor | En contra |
|---|---|---|
| Qt en todo (QObject, QUndoStack, QJson) | Arranque más rápido | El núcleo no se prueba sin Qt |
| **Núcleo en C++ puro, Qt solo en `app/` y `render/`** ✅ | Núcleo probado con pruebas unitarias; posible uso sin ventana (`--dump`) | Una plantilla `Signal<>` propia (pocas líneas) |

Se descartaron también Qt Quick/QML (incrustar el visor OpenGL de OCCT es más complejo), Dear ImGui (bueno para
prototipos, no para una app de escritorio pulida) y GTK/wxWidgets (menos ejemplos con OCCT).

### Archivo `.nuf` sobre SQLite

| Opción | En contra |
|---|---|
| Zip con JSON | Cada guardado reescribe todo; un corte a mitad puede corromperlo |
| Un solo JSON | Malo para binarios (sólidos importados, miniatura) |
| Formato de OCAF | Solo tiene sentido con OCAF |
| Binario propio (CBOR, Protobuf) | Más trabajo, ilegible, sin las garantías de SQLite |
| **SQLite + JSON** ✅ | No se lee con un editor de texto (se mitiga con `NufioCAD --dump`) |

Reglas del formato (decididas en la Fase 4):

| Tema | Decisión | Por qué |
|---|---|---|
| Dónde vive la serialización | En `io/`, una función por tipo de operación; no `save()`/`load()` en `Feature` | `document/` no depende de JSON |
| Enumeraciones | Como texto (`"Cut"`), nunca como número | Reordenar un `enum` no cambia el significado de archivos viejos |
| Cuándo se escribe | Solo al guardar, en una transacción; el archivo no se queda abierto | «No guardar» debe funcionar; un archivo abierto choca con Windows y OneDrive. La copia de recuperación hace el papel del autoguardado |
| Geometría | No se guarda; se recalcula al abrir (salvo los sólidos importados, en BREP binario) | Los parámetros son la fuente de verdad |
| Importar STEP | Se guarda el sólido ya reparado, no la ruta | El STEP original se puede mover o borrar |
| `.f3d` de Fusion 360 | **Descartado** | Formato cerrado de Autodesk, con un kernel que OCCT no lee |

### Compilación nativa en cada sistema (no Docker)

Docker está pensado para servicios: en Windows corre sobre WSL2, la interfaz gráfica necesita reenviar el display y el
OpenGL del visor es frágil. Todo el stack (C++, Qt, OCCT, SQLite, CMake) ya es multiplataforma, así que se compila de
forma nativa en Windows (MSVC + vcpkg) y Linux (GCC + paquetes del sistema o vcpkg).

### Cálculo en segundo plano

En la Fase 0 quedó como «cuando los modelos lo pidan». **Se hizo tras las pruebas** (una rosca cortante colgaba la
app): el historial, las vistas previas y la malla de los cuerpos pesados se calculan en hilos sobre copias, y un
cambio nuevo cancela el anterior. Desde la Fase 13 (F4) todos usan **un solo grupo de hilos** (`geometry::TaskPool`, sin Qt) en lugar de un hilo por tarea: cancelar una tarea en cola la descarta, los hilos se reutilizan y al cerrar se cancela y se espera de un solo sitio. Lo que falta está en [`auditorias/auditoria-3-fluidez.md`](auditorias/auditoria-3-fluidez.md).

## Detalles de plataforma que condicionan el código

- **Wayland (Linux):** el OCCT de las distribuciones usa GLX y no adopta el contexto EGL de Qt en Wayland nativo;
  `main.cpp` fuerza XWayland (`QT_QPA_PLATFORM=xcb`) salvo que el usuario lo defina.
- **Versiones de OCCT:** las bibliotecas de STEP y STL cambian de nombre en la 7.8 (`TKDESTEP` frente a `TKSTEP`) y
  algunas funciones del visor en la 7.7; `src/geometry/CMakeLists.txt` y `#if` por versión lo resuelven.
- **MSVC en español:** ninja no reconoce «Nota: inclusión del archivo» y deja de seguir los cambios en cabeceras; el
  hay que compilar con los mensajes en inglés (`VSLANG=1033`; hoy es un paso manual, no está en los presets). Ver
  [`desarrollo.md`](desarrollo.md).
- **Smart App Control (Windows):** bloquea las DLL sin firmar de OCCT en Release; por eso casi todas las medidas de
  Windows son de Debug ([`rendimiento/mediciones.md`](rendimiento/mediciones.md)).

## Distribución y firma de código (Windows)

Decidido el 16 nov. 2026; sin hacer todavía (ver la Fase 19 de la [hoja de ruta](hoja-de-ruta.md)).

- **No hay una auditoría obligatoria de Microsoft** para un programa de escritorio. Lo que decide si Windows lo trata como amenaza son tres mecanismos automáticos:
  **SmartScreen** (reputación del archivo y del firmante; avisa pero deja seguir), **Defender** (firmas, heurísticas y aprendizaje automático en la nube; da falsos positivos, sobre todo con código que se genera en ejecución) y **Smart App Control** (Windows 11: solo ejecuta binarios firmados por una entidad de confianza o con buena reputación, sin opción de ejecutar de todas formas).
- **Firmar con Authenticode** es lo que lo resuelve: el `.exe`, **todas las DLL** y el instalador, con marca de tiempo. La reputación pertenece al certificado, así que sobrevive de una versión a otra. Un certificado autofirmado no sirve para Smart App Control. Desde 2023 la clave privada debe estar en un token o en un servicio en la nube.
- **Opciones de firmante** (precios y disponibilidad por país cambian; verificar al contratar): firma en la nube de Azure (unos 10 USD al mes), certificado OV (cientos de dólares al año; el EV ya no da reputación inmediata en SmartScreen) y SignPath Foundation (gratis para proyectos abiertos).
- **La firma va en el CI**, con secretos del repositorio, para que la clave no esté en ningún PC.
- **Falsos positivos:** se envían a Microsoft con el portal de muestras de Defender (revisión gratuita). Se evita lo que los dispara: empaquetadores, ofuscadores y binarios que no se puedan reproducir desde el código público.
- **Con revisión real** solo hay dos vías: Microsoft Store (certificación y firma de Microsoft) y winget (se revisa el manifiesto y se escanea el instalador).
- **Para probar en esta PC** (Smart App Control bloquea las DLL sin firmar de OpenCASCADE en Release): probar en otra máquina o en una máquina virtual; apagarlo es irreversible en esa instalación de Windows.

## Referencias

- SQLite como formato de archivo de aplicación: <https://sqlite.org/appfileformat.html>
- El problema de nombrado topológico en FreeCAD: <https://wiki.freecad.org/Topological_naming_problem/en>
- Documentación de OCAF de OpenCASCADE (transacciones, TNaming, persistencia)
