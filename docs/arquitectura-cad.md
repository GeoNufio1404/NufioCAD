# Arquitectura — CAD paramétrico (Fase 0)

> Estado: **aprobada** tras evaluar alternativas (sección 10). Complementa `hoja-de-ruta-cad.md`.

## 1. Objetivos de la arquitectura

1. **El documento guarda parámetros, no geometría.** La forma 3D es un resultado recalculable (caché).
2. **Núcleo sin Qt.** Toda la lógica (documento, comandos, sketch, geometría, archivos) es C++ puro y se puede probar sin abrir una ventana.
3. **OCCT aislado detrás de interfaces propias.** Ningún módulo fuera de `geometry/` incluye cabeceras de OpenCASCADE directamente (salvo `render/`, que usa AIS).
4. **Todo cambio pasa por un comando.** Así deshacer/rehacer funciona siempre, sin casos especiales.
5. **Las referencias a caras y aristas son estables** (problema de nombrado topológico): se define la interfaz desde ahora, aunque la solución completa llegue en la Fase 3.

---

## 2. Capas

Las dependencias solo apuntan hacia abajo.

```
┌──────────────────────────────────────────────────────────┐
│  app/      UI Qt Widgets: ventana, navegador, timeline,  │
│            panel, menú radial, herramientas interactivas │
├──────────────────────────────────────────────────────────┤
│  render/   Viewport (QOpenGLWidget + V3d/AIS de OCCT):   │
│            selección, resaltado, vista previa            │
╞══════════════════════ NÚCLEO (sin Qt) ═══════════════════╡
│  commands/ Command + CommandStack (deshacer/rehacer)     │
├──────────────────────────────────────────────────────────┤
│  document/ Document, Feature, Timeline, Parameters,      │
│            motor de recálculo, referencias estables      │
├───────────────────────────┬──────────────────────────────┤
│  sketch/  geometría 2D +  │  io/  .nuf (SQLite), STEP,   │
│  restricciones (ISolver)  │       STL, OBJ               │
├───────────────────────────┴──────────────────────────────┤
│  geometry/ Adaptador del kernel (OCCT): extruir,         │
│            booleanas, fillet, mallado, historial topo.   │
└──────────────────────────────────────────────────────────┘
   OpenCASCADE · SQLite · libslvs / PlaneGCS · Qt (solo arriba)
```

| Módulo | Responsabilidad | Depende de |
|---|---|---|
| `geometry/` | Envolver OCCT: operaciones sólidas, mallado, historial de caras/aristas | OCCT |
| `sketch/` | Entidades 2D, restricciones, detección de perfiles cerrados | `geometry/`, solver |
| `document/` | Árbol de operaciones, parámetros, recálculo, referencias | `geometry/`, `sketch/` |
| `commands/` | Cambios reversibles sobre el documento | `document/` |
| `io/` | Guardar/abrir `.nuf`; exportar STL/OBJ/STEP; importar STEP | `document/`, `geometry/`, SQLite |
| `render/` | Mostrar resultados y vistas previas; traducir clics a referencias | `document/`, OCCT AIS, Qt |
| `app/` | Ventanas, paneles, atajos, herramientas | todo lo anterior, Qt |

---

## 3. Modelo del documento

```cpp
// Identificador estable de una operación (no cambia al reordenar)
using FeatureId = std::uint64_t;

class Feature {
public:
    virtual ~Feature() = default;
    FeatureId id() const;
    std::string name() const;          // "Extrusión2"
    bool suppressed() const;

    // Calcula su resultado a partir del estado anterior del modelo
    virtual ComputeResult compute(const ModelState& before,
                                  ComputeContext& ctx) = 0;

    // Serialización de PARÁMETROS (no de geometría)
    virtual void save(Writer& w) const = 0;
    virtual void load(Reader& r) = 0;
};

// Ejemplos: SketchFeature, ExtrudeFeature, RevolveFeature,
//           FilletFeature, ChamferFeature, ImportStepFeature
```

```cpp
class Document {
public:
    Timeline&    timeline();     // lista ordenada de Feature + marcador
    Parameters&  parameters();   // grosor = 3 mm, expresiones (Fase 6)
    const ModelState& result() const;  // caché de la última reconstrucción

    void recompute(FeatureId from);    // reconstruye desde 'from' hasta el marcador

    // Notificaciones (la UI se suscribe; el núcleo no conoce Qt)
    Signal<FeatureId> featureChanged;
    Signal<>          recomputed;
};
```

- **`ModelState`**: los cuerpos resultantes (formas OCCT envueltas) más el mapa de nombres topológicos.
- **Timeline**: orden de operaciones + **marcador** (lo que está después del marcador no se calcula).
- **Recálculo**: al editar la operación *k*, se reutiliza el `ModelState` en caché de *k-1* y se recalcula de *k* al marcador. Si una operación falla, queda marcada con error (como en Fusion) y las siguientes se muestran en advertencia, sin perder datos.
- **`Signal<>`**: plantilla propia mínima (o una biblioteca header-only), sin depender de Qt.

---

## 4. Comandos y deshacer/rehacer

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Document& doc) = 0;
    virtual void undo(Document& doc) = 0;
    virtual std::string label() const = 0;   // "Editar Extrusión2"
};

class CommandStack {
public:
    void push(std::unique_ptr<Command> cmd);  // ejecuta y apila
    void undo();
    void redo();
};
```

Comandos iniciales: `AddFeature`, `EditFeatureParams`, `DeleteFeature`, `SuppressFeature`, `ReorderFeature`, `MoveMarker`, `EditSketch` (agrupa cambios del sketch).

- **Implementación por memento:** cada comando guarda una copia de los **parámetros serializados** de las operaciones afectadas, antes y después. Deshacer = restaurar esa copia y recalcular. Es simple, difícil de romper y barato (los parámetros pesan pocos KB).
- Los comandos nunca guardan geometría.
- Tras `execute`/`undo` se llama a `recompute` desde la operación afectada.

---

## 5. Herramientas interactivas y vista previa

Cada herramienta de la UI (Extruir, Línea, Fillet…) es una pequeña **máquina de estados** en `app/`:

```
Inactiva → Seleccionando entrada → Ajustando parámetros (vista previa) → Aceptar / Cancelar
```

- La **vista previa** crea una operación temporal y la calcula con `compute()` **fuera** del documento; `render/` la dibuja en naranja.
- Solo al **Aceptar** se emite un `AddFeature` (o `EditFeatureParams`) al `CommandStack`. Cancelar no deja rastro en el historial.

---

## 6. Referencias estables (nombrado topológico)

Las operaciones nunca guardan "arista 7". Guardan una **referencia descriptiva**:

```cpp
struct TopoRef {
    FeatureId   generatedBy;   // operación que creó el elemento (ej. Extrusión1)
    std::string role;          // "cara_lateral", "cara_tapa", "arista_lateral"…
    std::string source;        // de qué entidad viene (ej. línea 3 de Sketch1)
};

class TopoResolver {
public:
    // Convierte un TopoRef en la cara/arista actual tras reconstruir
    std::optional<Subshape> resolve(const TopoRef& ref, const ModelState& s);
    // Lo contrario: lo que el usuario seleccionó → TopoRef
    TopoRef describe(const Subshape& picked, const ModelState& s);
};
```

- Se apoya en el historial que entregan las operaciones de OCCT (`Generated()` / `Modified()` de `BRepBuilderAPI_MakeShape`).
- **Decisión para la Fase 3:** implementar `TopoResolver` con TNaming de OCCT (usado solo dentro de `geometry/`) o con un resolvedor propio. La interfaz permite cambiar sin tocar el resto.

---

## 7. Formato `.nuf`: un archivo SQLite

Un solo archivo de base de datos SQLite, con los parámetros en JSON dentro de las tablas:

```sql
-- Identifica el archivo como .nuf y su versión de esquema
PRAGMA application_id = <número propio>;
PRAGMA user_version  = 1;

CREATE TABLE meta       (key TEXT PRIMARY KEY, value TEXT);        -- unidades, versión de la app
CREATE TABLE features   (id INTEGER PRIMARY KEY, position INTEGER,
                         type TEXT, name TEXT, suppressed INTEGER,
                         params TEXT);                              -- params en JSON
CREATE TABLE parameters (name TEXT PRIMARY KEY, expression TEXT);   -- Fase 6
CREATE TABLE blobs      (key TEXT PRIMARY KEY, kind TEXT, data BLOB); -- importados, caché BREP, miniatura
```

- La fuente de verdad son `features` y `parameters`; la caché BREP se puede borrar y se regenera.
- **Guardado atómico**: un corte de luz a mitad del guardado no corrompe el archivo.
- **Autoguardado barato**: cada cambio se escribe solo en las filas afectadas, lo que permite recuperar trabajo tras un cierre inesperado.
- **Migraciones**: `user_version` indica qué versión del esquema tiene el archivo.
- **Depuración**: se puede abrir con la herramienta `sqlite3`; además se agregará un comando `nuf-dump` que exporta el documento a JSON legible.

---

## 8. Flujo completo: extruir un perfil

1. El usuario presiona **E** → `app/` activa `ExtrudeTool`.
2. Selecciona un perfil → `render/` convierte el clic en un `TopoRef` vía `TopoResolver::describe`.
3. Arrastra la flecha → la herramienta crea un `ExtrudeFeature` temporal, llama a `compute()` y `render/` muestra la vista previa.
4. **Aceptar** → `CommandStack::push(AddFeature(...))`.
5. `AddFeature::execute` inserta la operación tras el marcador y llama a `Document::recompute`.
6. `Document` emite `recomputed` → `render/` actualiza el viewport y `app/` el navegador y el timeline.
7. **Ctrl+Z** → `AddFeature::undo` la quita y se recalcula.

---

## 9. Decisiones abiertas

| Tema | Opciones | Cuándo |
|---|---|---|
| Nombrado topológico | TNaming de OCCT vs. resolvedor propio (detrás de `TopoResolver`) | Fase 3 |
| Solver 2D | `libslvs` vs. PlaneGCS | Fase 2 |
| Recálculo en segundo plano | Hilo de trabajo para no congelar la UI | Cuando los modelos lo pidan (Fase 5+) |
| Biblioteca JSON | Ej. nlohmann/json | Fase 4 |

---

## 9.1 Multiplataforma: Windows y Linux

La app se compila de forma **nativa** en cada sistema a partir del mismo código (ver 10.6). Reglas:

**Compilación**
- **CMake** como único sistema de build, con `CMakePresets.json` (un preset por sistema: `linux-gcc`, `windows-msvc`, en Debug y Release).
- **vcpkg en modo manifiesto** (`vcpkg.json` en la raíz) para fijar las mismas versiones de OCCT, SQLite y bibliotecas auxiliares en ambos sistemas. Los paquetes de Fedora sirven para desarrollar rápido en Linux, pero la referencia son las versiones fijadas en vcpkg. (OCCT incluye su propio manifiesto de vcpkg en su repositorio.)
- **Qt 6**: por vcpkg o con el instalador oficial; la decisión se toma al montar el entorno, porque compilar Qt desde vcpkg tarda mucho.
- **C++20** con compiladores MSVC (Windows) y GCC/Clang (Linux), y advertencias tratadas como errores en ambos.

**Código**
- **Rutas**: siempre `std::filesystem::path`; nunca concatenar con `/` o `\` a mano ni asumir mayúsculas/minúsculas en nombres de archivo.
- **Texto**: UTF-8 en todo el núcleo; conversión a `QString` solo en `app/`. En Windows, abrir archivos con la ruta como `path` (no `char*`) para soportar acentos en carpetas.
- **Nada de APIs exclusivas de un sistema en el núcleo** (`unistd.h`, `windows.h`). Si algo depende del sistema (carpeta de configuración, archivos recientes, asociación de `.nuf`), vive en `app/platform/` y usa Qt (`QStandardPaths`, etc.).
- **Fin de línea**: `.gitattributes` normaliza a LF en el repositorio.
- **Pantallas HiDPI** y escalado de Windows: soportados por Qt 6; los iconos serán SVG.

**Gráficos**
- El viewport usa OpenGL a través de OCCT dentro de `QOpenGLWidget`, igual en ambos sistemas.
- Riesgo a vigilar: en Linux (Fedora usa Wayland por defecto) hay que probar el visor desde la Fase 1; si da problemas, se puede forzar XWayland (`QT_QPA_PLATFORM=xcb`) mientras se resuelve.

**Distribución**
- **Windows**: `windeployqt` para reunir las DLL de Qt + instalador (CPack con NSIS o WiX), con asociación de `.nuf` y entrada en el menú Inicio.
- **Linux**: AppImage (un solo archivo ejecutable) y, más adelante, Flatpak o RPM.

**Integración continua (CI)**
- Cada commit compila y corre las pruebas del núcleo en **Windows y Linux** (por ejemplo, GitHub Actions con una matriz de dos sistemas). Así un cambio que rompe un sistema se detecta de inmediato.

---

## 10. Alternativas evaluadas

### 10.1 Modelo de documento, deshacer y nombrado

| Opción | A favor | En contra |
|---|---|---|
| **A. OCAF completo** (framework de documentos de OCCT) | Trae transacciones con deshacer/rehacer, recálculo por dependencias (TFunction), nombrado topológico (TNaming) y persistencia binaria/XML | Hay que diseñar todo el árbol de etiquetas a mano; las etiquetas no se pueden borrar con el documento abierto (incómodo para borrar operaciones); atributos grandes se respaldan completos en cada cambio; los atributos propios necesitan drivers de persistencia; amarra todo el modelo al kernel |
| **B. Modelo propio** con comandos | Control total, simple de entender y probar, independiente del kernel | Hay que resolver el nombrado topológico por cuenta propia |
| **C. Híbrido** ✅ | Modelo y comandos propios (B), y TNaming de OCCT disponible como implementación de `TopoResolver` dentro de `geometry/` | La integración de TNaming hay que hacerla con cuidado en la Fase 3 |

**Elegida: C.** Toma lo mejor de OCAF (su nombrado topológico) sin heredar su rigidez. FreeCAD también usa su propio modelo de documento y no OCAF, y aun así tardó dos décadas en mitigar el problema de nombrado (versión 1.0, y solo en Sketcher y PartDesign): por eso la interfaz `TopoResolver` se define desde el día uno.

### 10.2 Deshacer/rehacer

| Opción | A favor | En contra |
|---|---|---|
| Comandos con lógica inversa a mano | Mínima memoria | Cada comando tiene su propia lógica de deshacer: fuente de errores |
| Copia completa del documento en cada paso | Robustísimo | Desperdicia memoria cuando hay muchas operaciones o sólidos importados |
| **Comandos con memento de parámetros** ✅ | Robusto, barato, con etiquetas legibles ("Editar Extrusión2") | Requiere que cada operación se serialice bien (lo necesitamos igual para guardar) |

### 10.3 Dónde vive Qt

| Opción | A favor | En contra |
|---|---|---|
| Qt en todo el código (QObject, señales, QUndoStack, QJson) | Arranque más rápido, utilidades listas | El núcleo no se prueba sin Qt; cambiar de UI sería reescribir todo |
| Núcleo con QtCore (sin GUI) | Señales y JSON de Qt en el núcleo | Sigue amarrando el núcleo a Qt |
| **Núcleo en C++ puro, Qt solo en `app/` y `render/`** ✅ | Núcleo probable con pruebas unitarias, portable, posibilidad de una versión sin interfaz (ej. convertir `.nuf` a STL por línea de comandos) | Hay que escribir una plantilla `Signal<>` mínima (pocas líneas) |

### 10.4 Toolkit de interfaz

| Opción | A favor | En contra |
|---|---|---|
| **Qt 6 Widgets** ✅ | Maduro, docking de paneles, atajos, menús; OCCT trae ejemplos de visor dentro de Qt; widgets propios (timeline, menú radial) se dibujan con QPainter | Estética por defecto sobria (se ajusta con estilos) |
| Qt Quick / QML | Animaciones y estética moderna | Incrustar el visor OpenGL de OCCT en QML es más complejo; dos lenguajes (C++ + QML) |
| Dear ImGui | Rapidísimo para prototipos y herramientas internas | Modo inmediato: menos natural para una app de escritorio pulida (diálogos de archivos, accesibilidad, texto) |
| GTK / wxWidgets | Nativos en Linux / multiplataforma | Menos ejemplos con OCCT; comunidad CAD más pequeña |

**Elegida: Qt 6 Widgets.** ImGui queda solo como opción para prototipos rápidos de alguna herramienta.

### 10.5 Contenedor del archivo `.nuf`

| Opción | A favor | En contra |
|---|---|---|
| Zip con JSON (propuesta inicial) | Legible, simple, compacto | Cada guardado reescribe el archivo completo; un corte de luz a mitad del guardado puede corromperlo si no se hace con cuidado |
| **SQLite** ✅ | Guardado atómico, escrituras incrementales (autoguardado barato y recuperación tras cierres inesperados), versionado de esquema con `user_version`, un solo archivo | No se lee con un editor de texto (se mitiga con `sqlite3` y `nuf-dump`) |
| Formato de OCAF (.cbf / .xml) | Lo da OCCT gratis | Solo tiene sentido si se usa OCAF completo (descartado en 10.1) |
| Un solo JSON | Lo más simple y legible | Malo para blobs binarios (sólidos importados, caché, miniatura) |
| Binario propio (CBOR, Protobuf) | Compacto y rápido | Más trabajo, ilegible y sin las garantías de SQLite |

**Elegida: SQLite con parámetros en JSON.** Mantiene la legibilidad de los parámetros y suma las garantías de una base de datos. Aplica la recomendación de la documentación oficial de SQLite de usarlo como formato de archivo de aplicación.

### 10.6 Cómo llegar a Windows y Linux

| Opción | A favor | En contra |
|---|---|---|
| Distribuir la app dentro de Docker | Mismo entorno en todas partes | Docker está pensado para servicios, no para apps de escritorio: en Windows corre contenedores Linux sobre WSL2, la interfaz gráfica necesita reenviar el display (X11/Wayland) y la aceleración OpenGL del viewport es frágil; el usuario final tendría que instalar Docker |
| **Código portable compilado de forma nativa en cada sistema** ✅ | Todo el stack (C++, Qt, OCCT, SQLite, CMake) ya es multiplataforma; rendimiento y OpenGL nativos; instalación normal para el usuario | Hay que compilar y probar en ambos sistemas (se automatiza con CI) |

**Elegida: compilación nativa.** Docker se usa solo, si hace falta, como entorno reproducible de compilación para Linux en CI, nunca para distribuir la app.

### Referencias
- Documentación de OCAF (OCCT): transacciones, TNaming, TFunction y persistencia.
- SQLite: "SQLite As An Application File Format" — https://sqlite.org/appfileformat.html
- FreeCAD 1.0 y el problema de nombrado topológico — https://wiki.freecad.org/Topological_naming_problem/en
