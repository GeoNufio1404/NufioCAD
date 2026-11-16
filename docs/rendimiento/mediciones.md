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
