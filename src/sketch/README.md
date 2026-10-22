# sketch/

Geometría 2D y restricciones del sketcher (Fase 2). Ver
`docs/arquitectura-cad.md`, sección 2.

- `Sketch.hpp`: modelo del sketch (entidades, restricciones y cotas), sin Qt ni OCCT.
- `Solver.hpp`: interfaz `ISolver`; `makePlaneGcsSolver()` la implementa con
  PlaneGCS (`third_party/planegcs`), que solo se incluye en `PlaneGcsSolver.cpp`.
