# PlaneGCS

Solver de restricciones geométricas 2D del sketcher de
[FreeCAD](https://github.com/FreeCAD/FreeCAD) (`src/Mod/Sketcher/App/planegcs`).
NufioCAD lo usa detrás de `nuf::sketch::ISolver` (ver `src/sketch/`).

- **Origen:** FreeCAD, commit `d3f4da791daa9e94f340b81770d13f090ec679c6` (2026-10-21).
- **Licencia:** LGPL-2.1 o posterior (`LICENSE`). Se enlaza estáticamente;
  al distribuir binarios hay que ofrecer este código y permitir reenlazarlo.
- **Dependencias:** Eigen 3.3+ (probado con 3.4 y 5.0) y la biblioteca de hilos.

## Cambios respecto al original

Los cambios en `src/` están en `nufiocad.patch` (`a/` = FreeCAD, `b/` = esta copia):

- `#include "../../SketcherGlobal.h"` → `#include "SketcherGlobal.h"`.
- Sin Boost: se quitan dos cabeceras incluidas y no usadas, y el grafo de
  `boost::connected_components` de `GCS.cpp` se sustituye por un union-find.

Además, `compat/` sustituye a las cabeceras de FreeCAD que PlaneGCS incluye
(`SketcherGlobal.h`, `FCConfig.h`, `Base/Console.h`, `Base/Tools.h`), y
`PlaneGcsPrelude.h` hace las veces del `PreCompiled.h` de FreeCAD.

## Actualizar

1. Copiar `src/Mod/Sketcher/App/planegcs/*` de FreeCAD sobre `src/`.
2. `patch -p1 -d src < nufiocad.patch` (resolver a mano si no aplica).
3. Actualizar el commit de este README y regenerar el parche.
4. Compilar y pasar `ctest` (pruebas `sketch` del solver).
