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
ejecuta 33 pasos; lo que importa de cada paso es la **parada máxima de la interfaz** (la línea final de `informe.txt`
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
