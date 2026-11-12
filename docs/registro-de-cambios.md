# Registro de cambios — NufioCAD

Qué cambió en cada entrega, en qué archivos y **cómo volver atrás** si algo sale mal. Las entregas nuevas van arriba. Para el detalle de lo pendiente, ver la [hoja de ruta](hoja-de-ruta-cad.md); para probarlas, la [guía de pruebas](guia-de-pruebas.md).

**Cómo se vuelve atrás, en general**
- **Si la rama aún no está fusionada** en `develop`: no se fusiona y listo (se puede borrar con `git branch -D feature/<nombre>`; `develop` no se entera).
- **Si ya está fusionada:** `git revert -m 1 <commit de la fusión>` en `develop` crea un commit que deshace toda la entrega sin reescribir la historia (y se puede volver a aplicar después con otro `revert` de ese revert).
- **Si solo hay que quitar una parte:** cada entrada dice qué funciones o bloques cambió cada parte, para deshacerla a mano en una rama `feature/` nueva.
- **Versiones publicadas:** la etiqueta `v0.1.0` (rama `main`) es la primera versión estable; `git switch --detach v0.1.0` la deja a mano para comparar.

---

## Perpendicular y paralela al dibujar líneas (entrega 2, segunda parte) — 11 nov. 2026

- **Rama:** `feature/inferencias-sketch` (sobre la parte anterior). **Sin fusionar** hasta la prueba manual (guía I.12–I.16).
- **Qué hace:** en la herramienta **Línea**, si el tramo nuevo queda a menos de 3° de la **perpendicular** o de la **paralela** al tramo anterior de la cadena (o a la línea de cuyo extremo se parte), se endereza, se resalta la línea de referencia en naranja discontinuo y sale el símbolo **⊥** o **∥**; al clic se crea la restricción Perpendicular o Paralela (solo si `canAdd`). La horizontal/vertical, las alineaciones con puntos y los puntos existentes tienen prioridad; Mayús y Ctrl las suprimen (el endereza a H/V de siempre no cambia).

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Cálculo | `detail::relate` y `enum class Relation` en `src/sketch/src/ToolSupport.cpp/.hpp` |
| Herramienta Línea | `LineTool` en `src/sketch/src/Tools.cpp`: miembro `reference_`, parámetro `relation` de `commit`, `preview()` y `currentValues()` |
| Vista previa | `Preview` gana `guides`, `badge` y `badgeAt` (`Tools.hpp`); `Viewport::showSketchPreview` gana `badge` y `badgeAt` (`Viewport.hpp/.cpp`, `ViewportImpl.hpp`); `SketchSession::showPreview` |
| Pruebas | 3 casos nuevos en `tests/test_sketch_inference.cpp` |

- **Pruebas:** 224 unitarias y 34 pasos de la prueba de interfaz en verde en Windows (Debug).
- **Pendiente:** solo la herramienta Línea (no el rectángulo ni el arco); no usa una línea «despierta» lejana como referencia; el símbolo es un texto, no un icono dibujado; la prueba de interfaz no cuenta aún los píxeles.
- **Riesgos al volver atrás:** ninguno en archivos guardados: son las restricciones Perpendicular y Paralela de siempre.

---

## Alineaciones con línea naranja, estilo Onshape (entrega 2, primera parte) — 11 nov. 2026

- **Rama:** `feature/inferencias-sketch` (sale de `develop` en `7ae4c23`). **Sin fusionar** hasta la prueba manual (guía I.1–I.11).
- **Qué hace:** con una herramienta de dibujo, el cursor se **pega** (≈6 px) a la recta horizontal o vertical del origen o de los dos últimos puntos sobre los que pasó el cursor («despiertos»), y se dibuja esa recta **discontinua y naranja**. Con dos rectas, se pega al cruce. Al hacer clic se crea la restricción horizontal/vertical entre el punto nuevo y el de referencia. **Mayús** las suprime; **Ctrl** sigue dibujando libre; un punto existente cerca gana a la alineación.

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Motor | `Snap` gana `alignX`, `alignY` y `guides`; `SnapOptions` gana `alignPoints` y `alignTolerance`; bloque «Alineación» antes de la rejilla en `snap()` (`src/sketch/include/nuf/sketch/Snap.hpp`, `src/sketch/src/Snap.cpp`) |
| Restricción al clic | final de `detail::attach` y primer bloque de `detail::straighten` (`src/sketch/src/ToolSupport.cpp`): la restricción solo se añade si `canAdd`; no se duplica si la recta es la del propio inicio de la línea |
| Puntos despiertos y Mayús | `SketchSession::snapAt`, `cursorMoved`, `awake_` y `setTool` (`src/app/src/SketchSession.cpp/.hpp`) |
| Dibujo de la guía | `Viewport::showSketchPreview` gana el parámetro `guides` (`Viewport.hpp/.cpp`, `ViewportImpl.hpp`): usa el trazo discontinuo naranja que ya tenía la geometría de construcción |
| Pruebas | `tests/test_sketch_inference.cpp` (7 casos, añadido a `tests/CMakeLists.txt`) |

- **Pruebas:** 221 unitarias y 34 pasos de la prueba de interfaz, en verde en Windows (Debug). La prueba de interfaz **aún no cuenta los píxeles naranjas** de la guía (pendiente I8).
- **Riesgos al volver atrás:** ninguno en archivos guardados: las restricciones creadas son las horizontal/vertical de siempre.
## Cotas que no saltan y la seleccionada gana el hueco — 11 nov. 2026

- **Rama:** `feature/cotas-e-inferencias` (sale de `develop`). Sin fusionar hasta la prueba manual (guía Q).
- **Qué hace:** (1) cada cota **recuerda su posición** (el nivel que la apartó de otras) entre repintados y al resolver el sketch: ya no salta al mover un punto; se olvida al hacer zoom. (2) La cota **seleccionada** se coloca primero y toma su sitio natural; las demás ceden. Lo que se dibuja y lo que se pica comparten la misma memoria (`Viewport::sketchAnnotations`).
- **Dónde:** `LayoutMemory` y `layoutAnnotations(…, memory)` en `src/sketch/{include/nuf/sketch,src}/Annotations.*`; la memoria vive en `ViewportImpl::layoutMemory`.
- **Pruebas:** 245 unitarias (nueva: repintar no mueve nada y la prioridad cambia el reparto) y 36 pasos de interfaz.
- **Pendiente:** línea de llamada cuando no queda sitio, ⊥/∥ para rectángulo y arco, referencia «despierta» lejana, arco tangente a una línea, y símbolos como iconos dibujados.
- **Riesgos al volver atrás:** ninguno en archivos guardados.

---

## Auditoría del sketch: eficiencia con sketches grandes — 11 nov. 2026

- **Rama:** `feature/auditoria-sketch` (sale de `develop`). **Sin fusionar**: los cambios son internos (mismos resultados, los 244 casos unitarios y los 36 pasos de interfaz siguen en verde), pero se prueban a mano con un sketch grande (guía P.1–P.3).
- **Cómo se midió:** banco `tests/bench_sketch.cpp` (oculto; se ejecuta con `nuf_tests "[.bench]"`) con una rejilla de 30 × 30 casillas (1 863 entidades), otra de 20 × 20 con 421 cotas y cadenas de líneas con restricciones. **Solo en Debug**: no hay compilación Release en esta máquina (pide recompilar todo vcpkg), así que las cifras absolutas son pesimistas; lo que vale es la comparación.
- **Qué se arregló:**
  1. `Sketch::entity(id)` buscaba recorriendo todas las entidades (y lo llaman `point`, `pick`, el snap y el solver por cada referencia): ahora las entidades se guardan **ordenadas por id** y se busca por **bisección** (`restore` las ordena y comprueba duplicados sin doble bucle).
  2. `findProfiles`: los vértices se buscaban uno a uno (**rejilla de celdas**), cada curva se comparaba con todas las demás (**se descartan por la caja**) y los tramos repetidos se comparaban de dos en dos (**mapa** por pareja de vértices).
  3. Colocación de cotas: la consulta de solapes llevaba la lista de cajas ya vistas con búsqueda lineal; ahora se marcan con el número de consulta.

| Operación (1 863 entidades, Debug) | Antes | Después |
|---|---|---|
| `pick` (resaltar al pasar el cursor sin herramienta) | 30,8 ms | 4,1 ms |
| Perfiles (`findProfiles`) | 4 598 ms | 120 ms |
| Colocar 421 cotas, zoom 0,05 | 52 ms | 31 ms |
| Colocar 421 cotas, zoom muy alejado (1) | 688 ms | 94 ms |
| `snap` por movimiento | 0,61 ms | 0,62 ms |
| Copiar el sketch (un paso de Deshacer) | 0,47 ms | 0,28 ms |

- **Solver medido en Release (11 nov. 2026):** compilando el sketch y PlaneGCS aparte con `/O2` (cadena de líneas unidas con coincidencias y longitud, un solo grupo): 25 líneas 1,6 ms, 50 → 6,2 ms, 100 → 31 ms, 150 → 107 ms, 300 → 1,67 s; un caso pequeño tarda 0,1 ms. Release es unas 30 veces más rápido que Debug; `pick` baja a 0,4 ms, perfiles de 1 863 entidades a 17 ms y colocar 421 cotas a 6 ms. **Conclusión:** el crecimiento sigue siendo cúbico, pero un grupo conectado de hasta ~100 entidades se resuelve en menos de un cuadro (≈30 ms); solo un grupo de 150 o más entidades notaría tirones al arrastrar. No se cambia el solver; se retoma si aparecen sketches reales con grupos así de grandes.
- **Riesgos al volver atrás:** ninguno en archivos guardados; las entidades siempre salen del archivo ordenadas por id.

---

## Ayuda en todas las herramientas y banner de la barra de estado — 11 nov. 2026

- **Rama:** `feature/ayudas-y-barra` (sobre `feature/pendientes-sketch`). Fusionada a `develop` a petición del usuario tras sus pruebas.
- **Qué hace:** (1) **todas** las herramientas de dibujo (líneas, rectángulos, círculos, arcos, polígonos, ranuras, elipses, splines, cónica, recortar/extender/partir, empalme/chaflán, desfase, simetría, mover/copiar/girar/escalar, patrones, proyectar, texto, imagen), las restricciones, la cota y las acciones de Archivo y Editar muestran al dejar el ratón encima el nombre, su tecla y **una línea de ayuda**; los menús también enseñan el tooltip (por defecto no lo hacen) y el botón «Restricciones» de la barra lleva el suyo. (2) La **barra de estado** pasa a un **banner azul** (#0B5CAD, texto blanco en negrita, filete claro arriba, 26 px de alto) **solo mientras hay un mensaje** (la indicación de la herramienta, avisos); sin mensaje vuelve a su aspecto neutro. El azul es el de la selección (como en Fusion 360), con contraste AA (6,6:1), y no usa rojo ni ámbar, que se reservan para errores.

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Textos de ayuda | tabla `help` en `MainWindow::createMenus` (`src/app/src/MainWindowMenus.cpp`) y `addHint` en `MainWindowCommon.hpp` |
| Menús con tooltip | último bucle de `createMenus` (`setToolTipsVisible`) |
| Banner | conexión a `QStatusBar::messageChanged` en el constructor de `MainWindow` (`src/app/src/MainWindow.cpp`) |
| Prueba | paso «Cada herramienta tiene su ayuda y la barra de estado destaca el mensaje» en `src/app/src/InterfaceCheck.cpp` |

- **Pruebas:** 244 unitarias y 36 pasos de la prueba de interfaz. Con otro programa pesado abierto (un juego) el paso «Mover y girar arrastrando» y el de la cota de un clic fallan por tiempo, sin relación con estos cambios; con la máquina libre pasan.
- **Pendiente:** las acciones de los diálogos, el árbol del modelo y las barras de vista no tienen tooltip nuevo; los avisos de error siguen con el mismo banner azul (se podría darles otro color si hace falta).
- **Riesgos al volver atrás:** ninguno en archivos guardados.

---

## Pendientes del sketch: cotas sobre el dibujo, puntos despiertos con espera y anillo, prueba de la guía — 11 nov. 2026

- **Rama:** `feature/pendientes-sketch` (junta `feature/figuras-abiertas` y `feature/inferencias-sketch-2`). **Sin fusionar** hasta la prueba manual (guía N.1–N.4).
- **Qué hace:** (1) los textos de las cotas y los iconos **se apartan de las líneas y puntos del dibujo** (pesa menos que chocar con otro texto: si no hay hueco, se queda donde menos tapa). (2) Un punto solo se **despierta** (referencia de las alineaciones) tras **0,3 s** de espera del cursor encima: pasar de largo ya no despierta nada. (3) Los puntos despiertos llevan un **anillo amarillo**. (4) La prueba de interfaz tiene un paso nuevo que **cuenta los píxeles naranjas** de la guía de alineación y comprueba que desaparece al alejar el cursor.

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Cotas sobre el dibujo | `addGeometryBoxes`, `kGeometryWeight` y el parámetro `geometry` de `placeAnnotation` en `src/sketch/src/Annotations.cpp` |
| Espera y anillo | `kAwakeDwellMs`, `dwellRef_` y `cursorMoved`/`showPreview` en `src/app/src/SketchSession.cpp/.hpp`; parámetro `awake` de `showSketchPreview` (`Viewport.hpp/.cpp`, `ViewportImpl.hpp`) |
| Prueba de la guía | paso «Guía naranja de alineación al dibujar» en `src/app/src/InterfaceCheck.cpp` |
| Pruebas | 1 caso nuevo en `tests/test_annotations_overlap.cpp` |

- **Pruebas:** 244 unitarias (2068 aserciones) y 35 pasos de la prueba de interfaz, en verde en Windows (Debug). Además, el build ya fuerza los mensajes del compilador en inglés (`VSLANG=1033`) para que ninja rastree las cabeceras.
- **Pendiente (decidido dejar):** recordar la posición de cada cota entre repintados (hace falta estado en el visor y que el picking use el mismo cálculo), prioridad a lo seleccionado o bajo el cursor, línea de llamada si nada queda libre, ⊥/∥ para rectángulo y arco, referencia «despierta» lejana para ⊥/∥, arco tangente a una línea desde la herramienta Arco, y dibujar el símbolo ⊥/∥/T como icono en vez de texto.
- **Riesgos al volver atrás:** ninguno en archivos guardados.

---

## Figuras abiertas — 11 nov. 2026

- **Rama:** `feature/figuras-abiertas` (sale de `develop`). **Sin fusionar** hasta la prueba manual (guía M.1–M.9).
- **Qué hace:** tras el **último clic** de un **polígono** (los tres modos), un **círculo** (centro y radio, por 2 y por 3 puntos), un **rectángulo** (por esquinas, por centro y por 3 puntos) y una **ranura**, la figura ya queda creada pero **abierta**: el recuadro junto al cursor sigue enseñando sus campos (lados y tamaño, diámetro, ancho y alto, longitud y alto, ancho de ranura) con su valor actual. Escribiendo y pulsando Enter se **rehace** con los nuevos valores (se fijan con cotas solo los que escribes; los demás se conservan). Se cierra con un **clic nuevo** (que ya empieza otra figura), con **Esc** (la figura se queda; otro Esc sale de la herramienta) o con **Enter sin escribir**.

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Estado común | `OpenFigure`, `entitiesSince` y `discardEntities` en `src/sketch/src/ToolSupport.hpp/.cpp` |
| Rectángulo y círculo | `RectangleTool` y `CircleTool` en `src/sketch/src/Tools.cpp` (`open_`, `reopen`) |
| Rectángulo por centro, por 3 puntos, polígono y ranura | `CenterRectangleTool`, `ThreePointRectangleTool`, `PolygonTool` y `SlotTool` en `src/sketch/src/ShapeTools.cpp` |
| Círculo por 2 y 3 puntos | `TwoPointCircleTool` y `ThreePointCircleTool` en `src/sketch/src/CircleTools.cpp` |
| Un solo paso en Deshacer | `SketchTool::amendedLast()` (`Tools.hpp`) y `SketchSession::applyTypedValues` |
| Pruebas | `tests/test_sketch_open_figures.cpp` (6 casos, añadido a `tests/CMakeLists.txt`) |

- **Pruebas:** 241 unitarias (2060 aserciones) y 34 pasos de la prueba de interfaz, en verde en Windows (Debug). Nota: el compilador sale en español y ninja no rastrea las cabeceras; tras cambiar un `.hpp` o de rama hay que limpiar `nuf_sketch` y `nuf_tests` (con objetos viejos salían «Stack around the variable corrupted» y SIGSEGV falsos).
- **Un solo paso en Deshacer:** corregir la figura recién creada no añade otro paso: Deshacer vuelve a antes de crearla.
- **Pendiente:** la elipse y el arco de ranura no tienen campos que editar (no cambia nada); al redimensionar se pierden las uniones del segundo clic con puntos existentes (las del primero se conservan).
## Tangente desde un arco e histéresis de la guía — 11 nov. 2026

- **Rama:** `feature/inferencias-sketch-2` (sale de `develop`). **Sin fusionar** hasta la prueba manual (guía L.1–L.3).
- **Qué hace:** (1) con **Línea**, si el primer tramo sale del extremo de un **arco** y queda a ≤3° de su tangente (hacia delante), se endereza, sale «T» y al clic se crea la restricción Tangente (que ya une los extremos, así que no se añade además la coincidencia). (2) La guía naranja de alineación se **mantiene hasta ~10 px** una vez pegada (se pega a 6 px), para que no tiemble al borde.

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Tangente | `arcExit_`, `relateEnd` y parámetro `tangent` de `commit` en `LineTool` (`src/sketch/src/Tools.cpp`) |
| Histéresis | `stickyX`, `stickyY`, `alignRelease` en `SnapOptions` (`Snap.hpp`), bucle de alineación de `snap` (`Snap.cpp`) y `SketchSession::snapAt` |
| Pruebas | 2 casos nuevos en `tests/test_sketch_inference.cpp` |

- **Pruebas:** 235 unitarias en verde en Windows (Debug).
- **Pendiente:** arco tangente a una línea desde la herramienta Arco; tangente con referencia lejana; la histéresis no cubre aún la alineación sobre curvas.
- **Riesgos al volver atrás:** ninguno en archivos guardados.

---

## Ajustes tras la primera prueba manual — 11 nov. 2026

- **Rama:** `feature/ajustes-sketch` (sale de la rama local de prueba que junta `feature/inferencias-sketch` y `feature/cotas-sin-solaparse`; esas dos deben fusionarse antes). **Sin fusionar** hasta la prueba manual (guía J.1–J.7).
- **Qué cambia:**
  1. **Cursor visible:** la cruz del sketch es ahora blanca con contorno oscuro y un hueco en el centro (la del sistema era negra y se perdía en el gris).
  2. **Guía naranja sobre líneas y ejes:** al llevar el cursor a una recta o circunferencia (ejes incluidos) el punto se desliza hasta donde esa curva corta la horizontal o la vertical de un punto de referencia, con su guía; al clic, restricción de punto sobre la curva y horizontal/vertical.
  3. **Punto medio:** además del punto amarillo, un **cuadrado naranja discontinuo** alrededor.
  4. **Título de la ventana:** muestra el nombre del archivo al abrir o guardar (antes el nombre interno del documento).
  5. **Cotas de radio/diámetro:** prueban hasta 12 posiciones (vuelta entera) y cuentan también sus líneas de cota, no solo el texto, para no pasar por encima de otros textos ni de otras cotas radiales.
  6. **Cotas de ángulo:** el arco se dibuja en la esquina de los segmentos dibujados (el vértice), y no en el cuadrante opuesto o complementario cuando las líneas se dibujaron «de cara» o «de espaldas».

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Cursor | `crossCursor()` y `Viewport::setSketchToolActive` en `src/render/src/Viewport.cpp` |
| Guía sobre curvas | `alignOnCurve` y rama «Sobre la curva más cercana» de `snap` en `src/sketch/src/Snap.cpp` |
| Cuadrado del punto medio | parámetro `markerSquare` de `showSketchPreview` (`Viewport.hpp/.cpp`, `ViewportImpl.hpp`) y `SketchSession::showPreview` |
| Título | `MainWindow::updateTitle` en `src/app/src/MainWindow.cpp` |
| Cotas radiales | `lineBoxes` y parámetro `radial` de `placeAnnotation` en `src/sketch/src/Annotations.cpp` |
| Cotas de ángulo | caso `ConstraintType::Angle` de `dimensionAnnotation` en el mismo archivo |
| Pruebas | 4 casos nuevos: 2 en `tests/test_sketch_inference.cpp`, 2 en `tests/test_annotations_overlap.cpp` |

- **Pruebas:** 233 unitarias (1969 aserciones) y 34 pasos de la prueba de interfaz, en verde en Windows (Debug).
- **Pendiente:** el cursor y el cuadrado solo se han comprobado por código, no a la vista; las cotas radiales aún no evitan las líneas del dibujo; el ángulo usa el valor de la restricción (no se «corrige» si el valor es el suplementario de la esquina visible).
- **Riesgos al volver atrás:** ninguno en archivos guardados.

---

## Cotas que no se pisan — 11 nov. 2026

- **Rama:** `feature/cotas-sin-solaparse` (sale de `develop`, independiente de las inferencias). **Sin fusionar** hasta la prueba manual (guía K.1–K.8).
- **Qué hace:** cada cota o icono de restricción se coloca después de calcular su sitio natural y, si su texto pisaría a otro ya colocado, se prueba otra posición (hasta 8): la cota lineal se aleja un renglón de la geometría, la de radio o diámetro gira 30°, la de ángulo usa un arco mayor, el icono pasa al hueco siguiente. Se colocan antes las cotas y luego los iconos.

| Parte | Dónde (para deshacerla a mano) |
|---|---|
| Colocación | `layoutAnnotations`, `placeAnnotation`, `Placed` y `textBox` en `src/sketch/src/Annotations.cpp` (el resultado conserva el orden de las restricciones) |
| Grado de libertad | parámetro `level` de `dimensionAnnotation` y de `alignedDimension` en el mismo archivo (con `level = 0` queda el dibujo de antes) |
| Pruebas | `tests/test_annotations_overlap.cpp` (5 casos, añadido a `tests/CMakeLists.txt`) |

- **Pruebas:** 219 unitarias (incluidas las nuevas) y 34 pasos de la prueba de interfaz, en verde en Windows (Debug). Colocar las anotaciones de un sketch de 1960 restricciones tarda unos 44 ms (Debug), antes unos 20 ms: el coste sube un poco por la comprobación.
- **Pendiente:** evitar también las líneas y puntos del sketch y las líneas de cota; dar prioridad a lo seleccionado o bajo el cursor; recordar la posición por cota; línea de llamada si nada queda libre.
- **Riesgos al volver atrás:** ninguno en archivos guardados (solo cambia dónde se dibujan los textos).

---

## Cotas sobre el lienzo, estilo Onshape (entrega 1) — 10 nov. 2026

- **Rama:** `feature/cotas-en-el-lienzo` (sale de `develop` en `d7e3ffa`). **Sin fusionar** hasta que pase la prueba manual (guía C.1–C.16).
- **Commit:** `0463e70` (más el de esta documentación).
- **Pruebas:** 213 unitarias y 34 pasos de la prueba de interfaz, todo en verde en Windows (Debug). Sin probar aún a mano ni en Fedora.

| Parte | Antes | Ahora | Dónde (para deshacerla a mano) |
|---|---|---|---|
| Valor de una cota | Diálogo modal «Cota» con el valor y botones Aceptar/Cancelar | Campo pequeño junto al cursor (ventana emergente), con el valor seleccionado; Enter acepta, Esc o un clic fuera cancelan; mismas expresiones con parámetros | `SketchSession::askDimensionValue` (`src/app/src/SketchSession.cpp`): el `QDialog` se cambió por un `QFrame` con `Qt::Popup` y un `QEventLoop`; clase `InlineEditorKeys` y función `dimensionCaption` en el espacio anónimo del mismo archivo |
| Herramienta Cota | D (o el botón) acotaba lo seleccionado; sin selección daba un aviso | Sin selección entra en el **modo Cota**: clic en círculo, arco, elipse o línea crea la cota; dos clics para distancias y ángulos; clic en una cota la edita; Esc quita lo elegido y luego sale. Con selección, D hace lo de antes | `SketchSession::Tool::Dimension` (`SketchSession.hpp`), `SketchSession::dimensionClicked`, y los cambios en `setTool`, `cancel`, `clicked` y `addDimension` (que ahora devuelve `bool`) |
| Cotas de círculos concéntricos | Todas a 45°: con radios parecidos los textos se apilaban | Cada círculo con el mismo centro que otro anterior pone su cota 30° más allá | Bloque `rank` en el caso `Radius`/`Diameter` de `src/sketch/src/Annotations.cpp` |
| Pruebas | — | Prueba unitaria «Las cotas de círculos con el mismo centro van a ángulos distintos…» y paso de la prueba de interfaz «Cota de un clic con el valor escrito sobre el lienzo» | `tests/test_sketch_selection.cpp`, `src/app/src/InterfaceCheck.cpp` |

**Cómo decide el modo Cota qué hacer con cada clic:**

```mermaid
flowchart TD
    A["Clic en modo Cota"] --> B{"¿Sobre una cota?"}
    B -- sí --> E["Reabrir el campo con su valor"]
    B -- no --> C{"¿Sobre una figura?"}
    C -- no --> D["Vaciar lo elegido"]
    C -- sí --> F{"¿Se puede acotar<br/>lo elegido?"}
    F -- "no, falta otro elemento" --> G["Esperar el siguiente clic"]
    F -- sí --> H["Campo de valor junto al cursor"]
    H -- Enter --> I["Crear la cota y resolver el sketch"]
    H -- "Esc o clic fuera" --> J["Cancelar: una línea queda elegida<br/>para emparejarla con otra"]
```

**Riesgos conocidos al volver atrás:** ninguno en archivos guardados: las cotas siguen siendo las mismas restricciones de siempre (no hay campos nuevos en el `.nuf`), así que un diseño hecho con esta entrega se abre igual en la versión anterior.

---

## Arreglo: rectas encimadas en el sketch — 11 nov. 2026

- **Rama:** `feature/cotas-en-el-lienzo`.
- **Qué pasaba:** en el sketch de `Prueba.nuf` había una recta larga con otras dos encima (que sumaban lo mismo) y un rectángulo colgando de la unión. Dos rectas sobre el mismo eje no «se cruzan», así que no se partían en la unión y el buscador de perfiles dejaba tramos duplicados y púas: salían 3 perfiles raros (25, 11 y 8 aristas, con un mismo tramo recorrido de ida y vuelta) en vez de las 9 regiones reales.
- **Qué cambió:** en `findProfiles` (`src/sketch/src/Profiles.cpp`) cada extremo de una recta que cae dentro de otra recta colineal la parte ahí, y los tramos de recta repetidos entre los mismos vértices se reducen a uno; el conteo de grados ignora los tramos descartados.
- **Prueba:** «Perfiles: rectas encimadas sobre el mismo eje no repiten tramos ni dejan púas» (`tests/test_sketch_profiles.cpp`). 214 pruebas unitarias en verde.
- **Volver atrás:** `git revert` del commit; no cambia el formato `.nuf`.

---

## Diagramas de la documentación a Mermaid — 10 nov. 2026

- **Rama:** `feature/cotas-en-el-lienzo` (solo documentación; no cambia código ni pruebas).
- **Qué cambió:** los dibujos hechos con caracteres de texto pasaron a [Mermaid](https://mermaid.js.org), que GitHub dibuja solo en cualquier archivo `.md`, y se añadieron los que ayudan a entender el flujo.

| Diagrama | Archivo | Antes | Ahora |
|---|---|---|---|
| Capas y dependencias de los módulos | `docs/arquitectura-cad.md` (sección 2) | Cuadro con caracteres de dibujo | `flowchart` con las capas (interfaz, núcleo, bibliotecas) y una flecha por cada dependencia de la tabla de debajo |
| Estados de una herramienta | `docs/arquitectura-cad.md` (sección 5) | Una línea con flechas de texto | `stateDiagram-v2` con Aceptar, Cancelar y Esc |
| Extruir un perfil | `docs/arquitectura-cad.md` (sección 8) | Solo la lista numerada | `sequenceDiagram` (usuario, herramienta, visor, comandos y documento); la lista se conserva debajo |
| Flujo de ramas | `docs/hoja-de-ruta-cad.md` (notas de planificación) | No existía | `gitGraph` con `main`, `develop` y las `feature/…` |
| Prioridad de las inferencias | `docs/hoja-de-ruta-cad.md` (plan I1) | No existía | `flowchart` con lo que gana cuando coinciden varias |
| Qué hace un clic en el modo Cota | `docs/registro-de-cambios.md` | No existía | `flowchart` de decisión |

- **Se dejó como está:** el árbol de carpetas del `README.md` (un árbol de archivos se lee mejor como texto que como diagrama) y los bloques de código y de comandos.
- **Comprobación:** los 6 diagramas se validaron con el propio analizador de Mermaid (versión 11), sin errores; el validador rechaza uno roto a propósito.
- **Volver atrás:** `git revert <commit>` de esta documentación, o `git checkout v0.1.0 -- docs/arquitectura-cad.md` para recuperar el dibujo de texto de ese archivo.

---

## Desde la v0.1.0 hasta esta entrega (ya en `develop`)

| Commit | Qué |
|---|---|
| `d7e3ffa` | Fusión de `feature/pruebas-flechas-robustas`: la prueba de las flechas cuenta el color exacto, reintenta el foco y el arco del ángulo de presión del engranaje es más grande |
| `896c86b` | Fusión de la extrusión hasta una cara curva (cilindro, esfera y toro) |
| `272404b` | Icono de la aplicación en el lanzador de Linux y en el `.exe` de Windows |
| `66ccf04`, `9d5f009`, `4dbe602` | Compatibilidad con Fedora y Ubuntu (OCCT 7.6 a 7.9, GCC) y el CI en `main`, `develop`, `feature/*` y pull requests |
| `b72757a` | Mediciones de rendimiento en Fedora ([`mediciones-rendimiento.md`](mediciones-rendimiento.md)) |
| `ab1fae7` | Marcadores `estable-windows` y `estable-fedora` en la hoja de ruta |
