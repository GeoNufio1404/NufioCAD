# Mediciones de rendimiento (Debug y Release)

Para comparar sistemas y versiones. La columna de Windows se llena cuando exista su build Release
(hoy Smart App Control bloquea las DLL sin firmar de OCCT; ver el backlog de la hoja de ruta).

## Cómo medir

Con la máquina en reposo (sin compilar ni otras apps pesadas; mirar `cat /proc/loadavg` < 1):

```bash
cmake --preset linux-release && cmake --build --preset linux-release     # o linux-debug
./build/linux-release/src/app/NufioCAD --prueba-interfaz --informe informe.txt
ctest --preset linux-release
```

En Windows, los mismos presets con prefijo `windows-` y `NufioCAD.exe`. `--prueba-interfaz` abre la ventana real y
ejecuta unos 44 pasos; lo que importa de cada paso es la **parada máxima de la interfaz** (la línea final de `informe.txt`
da la peor). El tiempo total de la prueba no sirve para comparar: lo dominan las esperas fijas de la propia prueba.

## Fedora 44 (9 nov. 2026, commit 9d5f009)

Equipo: Intel Core i7-8750H (12 hilos), 23 GB. Qt 6.11.2 y OCCT 7.9.3 **del sistema** (optimizados en ambos casos);
solo cambia la compilación del código propio (Debug = `-g`, Release = `-O3 -DNDEBUG`). Sesión Wayland con XWayland.

| Medida | Fedora Debug | Fedora Release | Windows Debug | Windows Release |
|---|---|---|---|---|
| Binario de la app | 92 MB | 4,9 MB | — | — |
| Compilación completa (12 hilos) | — | 1 min 13 s | — | — |
| `ctest` (205 pruebas) | 16,2 s | 14,0 s | — | — |
| `--prueba-interfaz`: pasos fallidos | 0 de 33 | 0 de 33 | — | — |
| Parada máxima de la interfaz | 899 ms | 150 ms | — | — |
| Suma de las paradas máximas de los 33 pasos | 3034 ms | 1505 ms | — | — |
| Cuadro de la vista vacía | 15 ms | 15 ms | — | — |
| Arrastre de gizmo: dibujo por paso | 31 ms | 31 ms | — | — |
| Pinturas del panel en reposo | 0 en 1 s | 0 en 1 s | — | — |

Pasos donde Debug y Release se separan (parada máxima de la interfaz):

| Paso | Debug | Release |
|---|---|---|
| Mover los dos cuerpos de un letrero a la vez | 899 ms | 144 ms |
| Generador «Letrero o llavero» | 881 ms | 150 ms |
| Resto de pasos (todos) | ≤ 99 ms | ≤ 97 ms |

## Lectura

- En Fedora, Release casi no cambia lo que se siente: el cuadro de la vista y el arrastre cuestan lo mismo, y solo
  el generador de texto (Letrero) es unas 6 veces más lento en Debug. El resto del tiempo lo ponen OCCT y Qt, que ya
  van optimizados.
- Por eso, la diferencia de fluidez entre Fedora y Windows **no se explica solo con Debug frente a Release del código
  propio**. La hipótesis que queda: en Windows OCCT y Qt también corren en Debug (`Qt6Widgetsd.dll`, bibliotecas de
  depuración de vcpkg), más el heap y los iteradores de depuración de MSVC. Se confirma o se descarta con la columna
  «Windows Release» de arriba.
- Los 33 pasos pasan en las dos configuraciones, así que Release no cambia el comportamiento, solo el tiempo.

## Fluidez: línea base de la Oleada 6 (Windows, Debug, 16 nov. 2026)

Medida con `NufioCAD.exe --prueba-interfaz` (44 pasos, 0 fallidos) en la rama `feature/fluidez-medir`, antes de tocar
nada. **Es Debug** (OCCT y Qt también): en Release se espera unas 5 a 6 veces menos (ver Fedora arriba), pero lo que
vale es el orden de magnitud y **qué parte del trabajo ocupa el hilo de la interfaz**. El objetivo de la Oleada 6 es
**menos de 100 ms en Release** (250 ms en Debug) en todo lo de esta tabla. Cada rama de la Oleada 6 repite la prueba y
añade su columna.

| Qué (paso de la prueba de interfaz) | Hallazgo | Parada de la ventana (Debug) |
|---|---|---|
| Par de engranajes: aceptar | A1 | 1280 ms |
| Engranaje interno: cambiar un valor | A1 | 1364 ms |
| Cremallera: aceptar | A1 | 849 ms |
| Letrero o llavero: abrir | A5 | 841 ms |
| Par de engranajes (flecha del módulo) | A1 | 1263 ms |
| Mover los dos cuerpos de un letrero | A1 | 773 ms |
| Engranaje (cambiar un valor) | A1 | 733 ms |
| Exportar: escribir el OBJ de una pieza con rosca | A2 | 797 ms |
| Exportar: escribir el STL / el STEP | A2 | 249 / 260 ms |
| Exportar: contar triángulos de una pieza con rosca | A3 | 205 ms |
| Diálogo de Exportar con 5 cuerpos: abrirlo | A3 | 348 ms |
| Diálogo de Exportar: marcar o desmarcar **un** cuerpo | A3 | 260–303 ms **cada vez** |
| Diálogo de Exportar: calidad Alta | A3 | 2200 ms |
| Importar un STEP (engranaje de 20 dientes) | A4 | 2381 ms |
| Medir dos caras de una rosca | M3 | 391 ms |
| Pantalla de inicio con 10 recientes (miniaturas pequeñas) | A8 | 87 ms |
| Patrón lineal de 50 copias: el cálculo completo, en segundo plano | M5 | 24 042 ms (la ventana: 993 ms tras agregarlo) |
| Cuadro de la vista vacía (referencia) | — | 15 ms |

Qué dicen las paradas largas (la pila que anota el vigilante):

- **A1 confirmado.** Las paradas de más de 1 s de los generadores de engranajes están en `BRepMesh_*`
  (`OSD_Parallel::For<BRepMesh_FaceDiscret…>`, `BRepMesh_Delaun`, `BRepMesh_DelaunayDeflectionControlMeshAlgo`) y en
  liberar memoria de la malla (`NCollection_IncAllocator::~NCollection_IncAllocator`): el visor malla los cuerpos
  normales al mostrarlos, en el hilo de la interfaz.
- **A3 confirmado.** La pila del diálogo de Exportar está en `BRepMesh_NodeInsertionMeshAlgo<BRepMesh_NURBSRangeSplitter…>`:
  `countTriangles` malla de nuevo todos los cuerpos marcados en cada clic.
- **A4 confirmado.** La importación se para en `ShapeFix_Solid::SolidFromShell` → `BRepClass3d_SolidClassifier` →
  `IntCurvesFace_Intersector` (la reparación del sólido), no en leer el archivo.
- **M5.** El patrón de 50 tarda 24 s en total en segundo plano (la ventana sigue viva, pero hay una parada de 1 s al
  mostrar el resultado): 49 uniones una a una.
- Fuera de OCCT: una parada de 1 s al arrastrar un anillo está en el controlador de gráficos (`DrvSwapBuffers`) y otra
  en `QFileInfo::filePath` (los iconos de la barra de ayuda): conviene vigilarlas, no son de esta oleada.

### Después de F2: llamadas que bloqueaban (rama `feature/fluidez-llamadas`, mismo equipo, Debug)

44 pasos, 0 fallidos. «Antes» es la tabla de línea base de arriba; «ahora» es la parada de la ventana en el mismo paso
(el trabajo sigue ocurriendo, pero en un hilo aparte).

| Qué | Antes | Ahora (ventana parada) | El trabajo, ahora en segundo plano |
|---|---|---|---|
| Exportar una pieza con rosca (el peor formato, OBJ) | 797 ms | 75 ms | 1232 ms |
| Exportar STL / STEP | 249 / 260 ms | dentro de los 75 ms | 519 / 534 ms |
| Diálogo de Exportar con 5 cuerpos: abrirlo | 348 ms | menos de 100 ms | 539 ms (conteo y revisión) |
| Diálogo: marcar o desmarcar un cuerpo | 260–303 ms cada vez | menos de 100 ms | 380 ms la primera vez; lo ya contado no se repite |
| Diálogo: calidad Alta | 2200 ms | menos de 160 ms | 3131 ms |
| Importar un STEP (engranaje de 20 dientes) | 2381 ms | 769 ms | 2530–3994 ms (varía entre ejecuciones) |
| Pantalla de inicio con 10 recientes | 87 ms | 75 ms | miniaturas pequeñas: casi nada |

Lo que queda de parada en esos pasos ya no es el trabajo de archivo: son los 0,6–1,5 s de mostrar cuerpos nuevos
(mallado del visor, hallazgo A1), que es lo que ataca F3. El tiempo total de exportar o importar no baja (incluso sube
algo por competir con la interfaz): lo que cambia es que la ventana no se queda sin responder.

### Después de F3: dibujo en segundo plano (rama `feature/fluidez-dibujo`, mismo equipo, Debug)

Tres ejecuciones completas (44 pasos, 0 fallidos en la última); entre ejecuciones los tiempos varían, por eso van como rango.

| Paso | Antes de F3 (tras F2) | Ahora |
|---|---|---|
| Generador «Par de engranajes» | 1263 ms | 529–856 ms |
| Generador «Engranaje interno» | 1396 ms | 591–683 ms |
| Generador «Cremallera» | 948 ms | 463–646 ms |
| Generador «Letrero o llavero» | 877 ms | 525–599 ms |
| Importar un STEP (engranaje) | 769 ms | 440–472 ms |
| Mover los dos cuerpos de un letrero | 885 ms | 512–774 ms |
| Patrón lineal de 50 copias | 973 ms | 707–824 ms |
| La parada más larga de toda la prueba | 1475 ms | 856–904 ms |

**Por qué siguen en 0,5–0,9 s (y por qué es de Debug).** Con el umbral de pila a 350 ms
(`NUF_PRUEBA_PILA_MS=350 NUF_PRUEBA_SOLO=Generador`), casi todas las paradas que quedan muestran el hilo de la interfaz
dentro de `RtlEnterCriticalSection` llamado desde `recalloc_dbg` o `free_dbg`: el montón de depuración de MSVC tiene un
solo candado para todo el proceso, y mientras los hilos de trabajo mallan en paralelo y reservan millones de bloques, la
interfaz espera para hacer cualquier reserva. No es trabajo de geometría en el hilo de la interfaz. En Release esa
contención no existe; se confirmará con la columna «Windows Release» (Oleada 7, D2).

### Después de F4: un solo grupo de hilos y cancelación en el kernel (rama `feature/fluidez-hilos`, mismo equipo, Debug)

F4 no mueve las cifras de las paradas de F3 (el trabajo ya estaba fuera del hilo de la interfaz): cambia **cuánto se
tarda en soltar el trabajo cancelado**. Lo que se comprueba es, sobre todo, estructural:

| Qué | Cómo se comprueba |
|---|---|
| Seis construcciones largas (rosca, engranaje helicoidal, redondeo, chaflán, vaciado, unión) abortan si ya se pidió cancelar | Prueba unitaria «Las construcciones largas se interrumpen…» |
| Cancelar una tarea en cola no la ejecuta; el grupo crea hilos solo si hacen falta y no pasa del máximo; destruirlo cancela y recoge todo | 6 pruebas unitarias del grupo (`[taskpool]`) |
| Prueba de interfaz completa | 44 pasos, 0 fallidos; parada máxima 955 ms (patrón de 50), generadores de engranajes 508–614 ms, igual que tras F3; el paso de la rosca con el banner ámbar y los spinners: 66 ms |

## Windows Release (16 nov. 2026)

Primera medición de **Release en Windows**, con Smart App Control apagado (ver la Oleada 7 de la
[hoja de ruta](../hoja-de-ruta.md)). Mismo equipo que las tablas de Debug de arriba, mismas 44 pasos de la prueba de interfaz.

| Medida | Windows Debug | Windows Release |
|---|---|---|
| Pruebas unitarias (265 casos) | ~130 s | **16 s** |
| Parada máxima de la interfaz (prueba completa, antes de F5) | 904–955 ms | 558 ms |
| Parada máxima tras F5 | — | **133 ms** |
| Pasos con parada de más de 100 ms | casi todos los de generadores y engranajes | 1 (la cota de un clic en el sketch: 133 ms) |
| Generador «Par de engranajes» | 529–856 ms | 27 ms |
| Generador «Letrero o llavero» | 525–599 ms | 78 ms |
| Mover los dos cuerpos de un letrero | 512–774 ms | 66 ms |
| Diálogo de Exportar con 5 cuerpos | 89–111 ms | 14 ms |
| Patrón lineal de 50 copias (cálculo completo, en segundo plano) | 24 042 ms | 2576 ms → **758 ms** tras F5 |
| Exportar un STL / OBJ / STEP (en segundo plano) | 519–1232 ms | 502 / 531 / 532 ms |
| Importar un STEP (engranaje, en segundo plano) | 2530–3994 ms | ~505 ms |
| Cuadro de la vista vacía | 15 ms | 15 ms |

**Lectura.** Confirma lo que se sospechaba en Debug: las paradas de 0,5 a 0,9 s eran sobre todo el candado del montón de
depuración de MSVC y desaparecen en Release (en Debug se medía el candado, no el trabajo de la interfaz). Quedaban dos
cosas reales, que F5 resolvió: el patrón de 50 copias (que se calculaba una a una) y **la primera vez que Qt dibuja un
símbolo** (⛔ ⚠ ℹ ✓), que paraba la ventana de 0,5 a 1,5 s al abrir el primer generador: ahora esa búsqueda de la fuente de
respaldo se hace al abrir la ventana (la parada de ese paso pasó a ~50 ms). Lo único que sigue pasando de 100 ms es
la cota de un clic en el sketch (133 ms). El cuadro de la vista vacía (15 ms) lo marca el controlador gráfico
(`DrvSwapBuffers`), igual que en Fedora.
