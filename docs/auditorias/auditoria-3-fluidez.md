# Tercera auditoría: fluidez («que nunca llegue a No responde»)

Hecha el 16 de noviembre de 2026 sobre `feature/engranajes-interno-cremallera` (commit `ab4080b`). **Solo plan: no se
cambió código.** Alcance: todo lo que la aplicación hace en el hilo de la interfaz y puede tardar, lo que se calcula dos
veces, las búsquedas con coste cuadrático y el uso de memoria. El trabajo sale como **Oleada 6 — Fluidez** en la
[hoja de ruta](../hoja-de-ruta.md).

## Objetivo medible

- **Release:** ninguna parada de la interfaz de más de **100 ms** en los pasos de la prueba de interfaz.
- **Debug:** ninguna de más de **250 ms**, salvo abrir un diálogo por primera vez.
- Cerrar la aplicación o abrir otro diseño en mitad de un cálculo: la ventana desaparece al instante.
- Se mide con el vigilante de pilas de `--prueba-interfaz` (ya existe) y se anota en
  [`rendimiento/mediciones.md`](../rendimiento/mediciones.md) antes y después de cada rama.

## Lo que ya va en segundo plano (no se toca)

| Qué | Dónde |
|---|---|
| Recálculo del historial (cancelable dentro de las booleanas) | `Document::startBackground` (`Document.cpp`) |
| Vistas previas de herramientas y generadores | `BackgroundRun` (`FeatureTools.cpp`, `GeneratorTool.cpp`) |
| Malla de los cuerpos «pesados» (rosca) | `ViewportImpl::startMeshTask` (`ViewportImpl.hpp`) |
| Abrir un `.nuf` e importar: el **cálculo** (no la lectura del STEP) | `whenComputed` (`MainWindowFiles.cpp`) |

## Línea base medida (F1, 16 nov. 2026)

La primera rama (`feature/fluidez-medir`) midió estos hallazgos con la prueba de interfaz y el vigilante, y **confirmó A1, A3 y A4 con la pila de llamadas**: paradas de 0,8–1,4 s en generadores de engranajes (mallado de OCCT en el hilo de la interfaz), 260–300 ms **por clic** en el diálogo de Exportar con 5 cuerpos (2,2 s al pasar a calidad Alta), 2,4 s al importar un STEP de un engranaje y 0,4 s al medir caras de una rosca. Cifras completas en [`../rendimiento/mediciones.md`](../rendimiento/mediciones.md). Todo es Debug en Windows.

## Hallazgos

Cada uno lleva dónde está, por qué para la interfaz y qué se propone. «Patrón conocido» = pasar la llamada a
`BackgroundRun` (o a su sustituto, F2), con «Calculando…» y cancelando al cambiar algo, como ya hacen las vistas
previas.

### Alta: pueden dejar la ventana en «No responde»

| # | Dónde | Qué pasa | Propuesta |
|---|---|---|---|
| A1 | `ViewportImpl::showBodies` (`ViewportImpl.hpp:267`) | Solo los cuerpos «pesados» se mallan aparte. El resto los malla `AIS_Shape` al hacer `Display`, y `Activate` calcula su selección (BVH), los dos **en el hilo de la interfaz**. Pasa al instalar cada resultado y **en cada paso de una vista previa**, porque cada paso trae un cuerpo nuevo. Es lo más probable detrás de las paradas de 1–2 s después de los generadores (anotadas en «Pendiente del cálculo en segundo plano») y de los 899 ms de «mover dos cuerpos» (hipótesis: se confirma con F1) | Preparar la malla en segundo plano para **todo** cuerpo nuevo (`prepareMesh` con la desviación normal del visor) y mostrarlo con la triangulación hecha (`SetAutoTriangulation(false)`). Activar la selección más tarde (cuerpo a cuerpo con `QTimer` 0 o al primer paso del cursor). Si la malla tarda menos de ~30 ms, no se nota el cambio |
| A2 | `MainWindow::exportBodies` y `printBodies` (`MainWindowView.cpp:316` y `:365`) | Mallar y escribir STL/OBJ/STEP con cursor de espera: con una rosca, varios engranajes o calidad Alta son segundos de ventana congelada | Patrón conocido: «Exportando…» en la barra y en el distintivo de la vista; aviso al terminar; no se puede exportar otra vez mientras tanto. El mallado (`BRepMesh`) y `STEPControl_Writer` aceptan progreso, así que se pueden cancelar |
| A3 | `ExportDialog::updateSummary` y `runReview` (`ExportDialog.cpp:287` y `:244`) | `countTriangles` **malla todos los cuerpos elegidos** cada vez que se marca una casilla o se cambia la calidad. `checkPrintable` hace una booleana `Common` por cada par de cuerpos que se tocan, un `BRepCheck_Analyzer` completo y la búsqueda de paredes (caras²). Todo en el hilo de la interfaz. Además, la malla del conteo se tira y al exportar **se vuelve a mallar** | Los dos con el patrón conocido (el diálogo ya enseña «Revisando la pieza…»). Guardar la malla por (cuerpo, calidad) y reutilizarla para escribir el archivo |
| A4 | `MainWindow::importStepFile` → `geometry::readStep` (`MainWindowFiles.cpp:366`) | Leer, convertir y reparar (`ShapeFix`) un STEP real tarda de segundos a minutos, en el hilo de la interfaz | Patrón conocido con «Importando…»; el comando se crea al terminar, como hoy. `STEPControl_Reader::TransferRoots` acepta progreso, así que se puede cancelar |
| A5 | `GeneratorTool::updatePreview` → `recipe()` (`GeneratorTool.cpp:281`); herramienta Texto (`SketchSession.cpp:530`) | La receta se arma **antes** de lanzar el hilo. En el Letrero eso incluye el contorno de las letras: `StdPrs_BRepFont::FindAndInit` (la primera vez recorre todas las fuentes del sistema) y `outlineOf`, que une contornos con booleanas. Explicaría los 881 ms del paso «Letrero» (hipótesis) | Armar la receta dentro del hilo: es código del documento, sin Qt. Preparar el gestor de fuentes de OCCT (`Font_FontMgr`) al arrancar, en segundo plano. **Antes, comprobar** que `StdPrs_BRepFont` se puede usar fuera del hilo principal (tiene su propio mutex; el gestor se inicia una sola vez) |
| A6 | `Document::stopBackground` (`Document.cpp:190`), `ViewportImpl::cancelMeshTasks` (`:250`) | Al cerrar o al abrir otro diseño se **espera** a los hilos. Hoy solo se pueden interrumpir dentro de las booleanas: una rosca larga, un engranaje helicoidal de muchos dientes o un redondeo grande dejan la ventana en «No responde» hasta que terminan | (1) Llevar la cancelación (`Message_ProgressRange`, que hoy solo usan la booleana y la malla) a `BRepOffsetAPI_ThruSections` (loft de la rosca y del engranaje), `BRepFilletAPI_MakeFillet`, `BRepOffsetAPI_MakeThickSolid`, `BRepOffsetAPI_MakePipeShell` y `fuseAll` (`Modeling.cpp:113`, que hoy no se puede cancelar ni corre en paralelo). Además, mirar `cancelRequested()` entre secciones en los bucles propios de la rosca y el engranaje. OCCT acepta progreso desde la 7.5, pero algunos algoritmos lo ignoran: comprobar cuáles se detienen de verdad. (2) Al cerrar, ocultar la ventana antes de esperar |
| A7 | `Recovery::write` (`Recovery.cpp:43`) cada minuto; `replaceFile` (`DocumentFile.cpp:916`) al guardar | La copia de recuperación serializa y escribe todo el diseño en SQLite en el hilo de la interfaz, con los BREP importados (pueden pesar MB): es un tirón cada minuto. Además, si OneDrive bloquea el archivo al guardar, `replaceFile` **duerme** hasta 2,25 s en ese hilo | Partir `saveDocument` en dos: «armar las filas» (JSON de cada operación, rápido, en la interfaz) y «escribir» (SQLite, cambiar el nombre, reintentos) en segundo plano. Guardar: el cursor de espera solo hasta tener las filas; un segundo guardado espera al primero. Recuperación: nunca bloquea y, si la anterior aún no terminó, se salta |
| A8 | `StartScreen::reload` (`StartScreen.cpp:200`) al arrancar | Abre cada `.nuf` reciente para leer su miniatura. Con los «Archivos a petición» de OneDrive (donde vive este proyecto), un archivo que solo está en la nube **se descarga entero** para eso, en el hilo de la interfaz | La lista sale al instante con un marcador y las miniaturas llegan en segundo plano, una a una. Un archivo que solo está en la nube (atributos `RECALL_ON_DATA_ACCESS`/`OFFLINE` en Windows) no se abre: se usa una copia local de la miniatura guardada al guardar |

### Media: tiempo de espera y CPU de más

| # | Dónde | Qué pasa | Propuesta |
|---|---|---|---|
| M1 | Aceptar en herramientas y generadores | La vista previa ya calculó el resultado con los mismos datos, y al aceptar **se calcula otra vez** en el documento. En los generadores es todo el diseño (se aplica la receta y se recalcula desde la primera operación nueva) | Si el documento no cambió desde la vista previa (misma generación y mismo marcador), instalar ese resultado sin recalcular (`Document::adoptPreview`). En los generadores, adoptar los estados del documento de trabajo. Ahorra la mitad de la espera con roscas, engranajes y generadores |
| M2 | `FeatureTool::showPreview` (`FeatureTools.cpp:395`) | Después del cálculo en segundo plano, vuelve a construir en el hilo de la interfaz el sólido que se quita (`tool(before())`: extrusión con conicidad, revolución, agujeros) | Calcularlo en el mismo hilo y pasarlo con el resultado |
| M3 | `MainWindow` Medir (`MainWindowView.cpp:301`) | `BRepExtrema_DistShapeShape` entre caras spline (una rosca) puede tardar segundos cada vez que cambia la selección | Patrón conocido con «Midiendo…» |
| M4 | Arrastre en el sketch (`SketchSession.cpp:880`, `ViewportImpl::showSketch` `:1230`) | En cada paso del arrastre se resuelve y, como la geometría cambió, se rehacen los perfiles (`findProfiles` y las caras de OCCT) | Durante el arrastre, rehacer el sombreado de regiones como mucho cada ~100 ms y siempre al soltar. Medir antes con el banco `[.bench]` |
| M5 | Patrón (`FeaturesMore.cpp:577–583`) y Unir con varios cuerpos (`Features.cpp:247`) | Las copias se unen **una a una**: hasta 199 booleanas, cada vez sobre un sólido más grande y cada una con `unify` y `BRepCheck_Analyzer`. El coste crece más que lineal con la cantidad | Una sola `BRepAlgoAPI_Fuse` con todas las copias como herramientas (`fuseAll`, con paralelo y cancelación añadidos) y `unify` una vez. Comprobar con las pruebas del patrón que las caras conservan su nombre (el comentario actual dice que una a una «el kernel las resuelve bien»: hay que confirmarlo con casos solapados) |
| M6 | `geometry::boolean` (`Modeling.cpp:690`) | Comprueba la validez dos veces (lo unido y, si falla, el original) | Medir en Release cuánto pesa; si `unify` no cambió nada, saltarse la comprobación |
| M7 | `Shape::findFace`/`faceRef`/`findEdge` (`Shape.cpp:44`), `Shape::bounds` (`:132`) | Cada búsqueda recorre todos los nombres y, si hay repetidos (patrones, `?<n>`), **calcula el centro de masa de cada cara repetida en cada consulta**. Se llama por cada arista de un redondeo, cada sketch sobre una cara, Medir, uniones… `bounds()` también recalcula la caja cada vez | Un índice nombre → índices ordenados, calculado una vez y guardado en `Shape::Impl` (la forma no cambia; con `std::call_once`, como `heavy`). La caja, igual |
| M8 | `document_.changed` → `MainWindow::refreshModel` (`MainWindowView.cpp:463`), `NavigatorWidget::refresh` (`ModelPanels.cpp:392`) | Con el cálculo en segundo plano, cada cambio refresca el modelo dos veces (al empezar y al terminar), y el navegador **se borra y se rehace entero** en cada una | Al empezar, refrescar solo el timeline y el título. El navegador actualiza filas, o se rehace solo si cambió la lista de cuerpos, sketches o planos |

### Baja: memoria y orden del código

| # | Dónde | Qué pasa | Propuesta |
|---|---|---|---|
| B1 | `triangulate` (`Exchange.cpp:49`) | Antes de escribir, guarda **todos** los triángulos en `double` (72 bytes cada uno: 1 millón son unos 72 MB más). El OBJ escribe cada vértice tres veces | Escribir cara a cara en el archivo (STL binario directamente en `float`) y, en el OBJ, soldar los vértices de cada cara |
| B2 | `ModelState` (`Feature.hpp:51`) | Cada estado del historial copia los mapas de sketches, planos y ejes: la memoria crece como operaciones × sketches | Medir con un historial de 200 operaciones (`test_perf.cpp`). Si pesa, compartir mapas que no cambian (`shared_ptr` con copia al escribir) |
| B3 | `BackgroundRun.cpp`, `Document.cpp:172–207` y `:656–773`, `ViewportImpl.hpp:173–265` | **Tres** copias del mismo patrón (hilo propio por tarea + cancelar + aviso por cola + lista de descartados). Cada malla, vista previa o recálculo abre un hilo nuevo. La auditoría 2 ya arregló un fallo de esta familia (nº 1) | Una sola utilidad sin Qt (`TaskRunner`: generación, cancelación y aviso con un `Poster`) sobre un grupo fijo de hilos (núcleos − 1, para que la interfaz siempre tenga uno). Unas 150 líneas menos y un solo sitio que revisar |
| B4 | `QFontComboBox` (`GeneratorTool.cpp:77`, `SketchSession.cpp:506`) | Recorre las fuentes del sistema cada vez que se crea (al abrir el Letrero o el Texto) | Cargar la lista una vez y reutilizarla |

### No se toca (medido o con poca ganancia)

- **Solver del sketch:** crece de forma cúbica, pero un grupo de ~100 entidades conectadas se resuelve en menos de un
  cuadro en Release (auditoría del sketch, 11 nov.).
- **Calcular en paralelo las operaciones independientes del historial:** es complejo (hay que saber qué depende de qué)
  y solo se hará si las mediciones lo piden después de M1 y M5.
- **Guardar la cámara, la caché BREP al abrir:** son funciones, no fluidez; siguen en sus bloques.

## Orden propuesto (cada uno en su rama, con medición antes y después)

| Paso | Rama | Contenido | Riesgo |
|---|---|---|---|
| 1 | `feature/fluidez-medir` | **F1:** vigilante de pilas también en uso normal (`NUF_VIGILANTE=1`: anota las paradas de más de 200 ms con su pila en la carpeta `cierres`) y pasos nuevos en la prueba de interfaz que reproduzcan cada hallazgo: exportar, el diálogo de exportar con 5 cuerpos, importar STEP, Medir sobre una rosca, Letrero, pantalla de inicio con 10 recientes, patrón de 50 | Ninguno (no cambia el comportamiento) |
| 2 | `feature/fluidez-llamadas` | A2, A3, A4, A7, A8, M3: todas son «llamada síncrona → patrón conocido» | Bajo |
| 3 | `feature/fluidez-dibujo` | A1, M2, A5 | Medio: cambia cómo se dibuja todo; la prueba de interfaz y la de las flechas lo vigilan |
| 4 | `feature/fluidez-hilos` | A6 + B3 (cancelación ampliada y un solo `TaskRunner`) | Medio: toca los tres sitios con hilos |
| 5 | `feature/fluidez-calculo` | M1, M5, M6, M7, M8 | Medio: M5 y M7 tocan los nombres de caras; las pruebas de nombres los protegen |
| 6 | — | B1, B2, B4 si las mediciones lo justifican | Bajo |

Cada rama deja su «Pendiente de …» en la hoja de ruta, sus pasos en la [guía de pruebas](../guia-de-pruebas.md) y su
entrada en el [registro de cambios](../registro-de-cambios.md), y no se fusiona hasta la prueba manual.

## Hecho y no marcado en la hoja de ruta (corregido en esta auditoría)

| Qué decía la hoja de ruta | Realidad | Commit |
|---|---|---|
| Mover y girar con el ratón, mover varios cuerpos: «EN PRUEBA, sin commit» | Hecho commit y en `develop`; solo falta la prueba manual O.1–O.18 | `4944e2e`, `80e9690`, `59a2476`, `79ecb4a` |
| Tapa en un solo cuerpo: «sin fusionar» | Fusionada | `bd6cc45` |
| Cotas sobre el lienzo (entrega 1): «sin fusionar» | Fusionada | `7ae4c23` |
| I3: faltan la espera y el anillo | Hechos (0,3 s y anillo amarillo) | `3d68eeb` |
| I4: faltan la referencia lejana y el icono dibujado | Hechos (falta ⊥/∥ con el arco) | `7ee7df2`, `04b30e0` |
| I6: faltan los puntos en amarillo | Hecho (anillo amarillo); falta el fondo del icono | `3d68eeb` |
| I8: faltan la histéresis y el paso de los píxeles naranjas | Hechos los dos | `229f1f1`, `3d68eeb` |
| S2: falta la línea de llamada | Hecha | `7ee7df2` |
| S5: sin hacer | A medias: la colocación evita líneas y puntos; falta el fondo opaco | `3d68eeb` |
| Auditoría del sketch: sin hacer | Hecha (bisección, perfiles, cotas, banco de medidas) | `fa455f7` |
| Oleada 2: compilar en Fedora y el CI de Ubuntu | Hecho; solo queda el filtro `*.exe` del slicer en Linux | `4dbe602`, `9d5f009`, `66ccf04` |
| Oleada 2: medir en Release | Hecho en Fedora y el solver en Windows; falta la app en Windows | `b72757a`, `18e5bec` |
| Estado: «205 pruebas, 33 pasos»; Fase 6 «pendiente: cotas ligadas» | 250 pruebas y 36 pasos; las cotas ligadas están hechas (Oleada 1) | — |
| Sin mención | Temas claro/oscuro con contraste, banner de la barra de estado, ayuda en todas las herramientas, iconos de restricciones dibujados | `4c081e4`, `3d48051`, `e9a47e1`, `04b30e0` |
