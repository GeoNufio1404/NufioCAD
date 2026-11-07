# Segunda auditoría de código (con usabilidad)

Hecha al cerrar las Fases 1 a 10 (6 de noviembre de 2026, sobre el commit `3aeaa88` y los cambios de esta auditoría).
Alcance: el código nuevo de las fases 5 a 10, lo que más riesgo tiene (hilos, comandos, unidades, generadores), todos los
«Pendiente de …» de la [hoja de ruta](hoja-de-ruta-cad.md) y lo que se notó al usar la aplicación.

## Cómo se hizo

- Compilación con `/W4 /WX` (sin avisos) y las 196 pruebas (1688 aserciones) en verde, antes y después.
- Lectura del código con riesgo de errores sutiles: mallado en segundo plano del visor, `AddRecipe` y los generadores,
  `Units` y `ExpressionSpinBox`, las nuevas primitivas y su guardado.
- Comprobación **en la aplicación** (con un arnés temporal que abre las herramientas y mide las pausas de la interfaz):
  el mallado en segundo plano sigue sin parar la interfaz después de los arreglos (largo 60 de rosca: 5 s de preparación,
  ninguna pausa).
- Repaso de los 36 «Pendiente de …» y de los textos de estado de la hoja de ruta para quitar lo que ya no es verdad.

## Arreglado en esta auditoría

| # | Hallazgo | Riesgo | Arreglo |
|---|---|---|---|
| 1 | El aviso de «malla terminada» del visor identificaba la tarea con un **puntero**: otra tarea nueva podía reutilizar la dirección y recibir el aviso de la anterior (bloqueando la interfaz con un `join`) | Parada de la interfaz, rara | Ahora cada tarea tiene un número único |
| 2 | El hilo de mallado compartía la **geometría** (superficies) con la interfaz y solo recogía las excepciones de OpenCASCADE | Carrera de datos poco probable; una excepción de otro tipo cerraba la aplicación | La copia incluye la geometría; se recoge cualquier excepción y se dibuja como siempre |
| 3 | Si la receta de un generador fallaba **al aceptar** (por ejemplo, un texto que ya no forma letras), la excepción salía sin recoger | Cierre de la aplicación | Se avisa en la barra de estado y la herramienta sigue abierta |
| 4 | La lista de cuerpos «sin malla en segundo plano» crecía sin límite (son direcciones) | Fuga lenta, e identificación errónea tras liberar memoria | Se vacía al pasar de 64 |
| 5 | Un cuadro de longitud trataba `1e3` como si llevara una unidad o un parámetro (la «e» es una letra) y no lo escalaba a la unidad elegida | Valor 10 veces mal con cm o pulgadas | La «e» de una notación científica no cuenta |
| 6 | El Letrero compartía el icono con la Caja con tapa | Confusión en la cinta | Icono propio |
| 7 | La hoja de ruta decía que la caja de una rosca ya era la ajustada (se revirtió: tardaba decenas de segundos) y mantenía como pendientes cosas ya hechas (el mallado en segundo plano, los demás generadores) | Documentación que engaña | Corregida |
| 8 | *(Hallado por la prueba de interfaz)* Al aceptar o cancelar una herramienta, su vista previa pendiente (la que espera 250 ms) se calculaba y se dibujaba **después** de cerrarla | Hasta 6 s de parada en Debug y un dibujo de más | `stopPreview()` al cerrar la herramienta |

## Abierto, por prioridad

### Alta

1. **Las pruebas manuales 1–38 y las de las fases 5–10 siguen sin hacerse en la ventana.** Es la mayor deuda: casi todo lo
   de la interfaz se ha comprobado por pruebas automáticas del núcleo y con capturas del arnés, no con el ratón. Seguir la
   [guía de pruebas](guia-de-pruebas.md) fase por fase.
2. ~~No hay prueba automática de la interfaz.~~ **Hecha** después de esta auditoría: `NufioCAD.exe --prueba-interfaz` (ver la
   guía de pruebas). Ya encontró un fallo real (nº 8 abajo). Falta llevarla al CI y que compruebe también cómo se ve.
3. **El build Release no se puede probar** (Smart App Control bloquea las DLL sin firmar). Todas las medidas de rendimiento
   son de Debug, que en OpenCASCADE es mucho más lento. Antes de seguir optimizando hay que medir en Release.

### Media

4. **Archivos muy grandes**: `Viewport.cpp` (2740 líneas), `FeatureTools.cpp` (2599), `MainWindow.cpp` (1769),
   `Features.cpp` (1243). Separar por responsabilidad (mallado en segundo plano, herramientas de cada familia, acciones de la
   ventana) bajaría el riesgo de tocar sin querer lo ajeno.
5. **Cotas del sketch ligadas a parámetros** y **renombrar un parámetro no actualiza las expresiones** que lo usan (fase 6c):
   hace que «paramétrico» sea solo la mitad de lo que parece, sobre todo con los generadores.
6. **Linux/Fedora y el CI de Ubuntu sin probar** desde la fase 4 (enlace de STEP, nlohmann-json, filtros `*.exe`).
7. **Los generadores no se reabren**: para cambiar algo hay que tocar parámetros y operaciones sueltas. Podría guardarse en el
   componente qué generador lo creó y con qué valores.
8. **Unidades**: faltan Medir, la revisión para imprimir, la lista de parámetros y los mensajes de error; las unidades por
   documento (hoy es una preferencia de la aplicación).
9. **3MF con varios cuerpos y filamentos por cuerpo** y **SVG como sketch**: lo piden las lightboxes de referencia.
10. `hasHeavySurface`/`hasMesh` recorren todas las caras de cada cuerpo en cada refresco del visor: sin problema con las
    piezas actuales (cientos de caras), pero crecerá con modelos grandes; guardar el resultado en la forma.

### Baja

11. Perfil de engranaje con pocos dientes (<10) puntiagudo; ruedas internas y cremallera.
12. Medir sin línea sobre el modelo, sin copiar al portapapeles; Sección solo con planos de origen.
13. La primera llamada a las herramientas pesadas en Debug se nota (2–3 s); en Release debería desaparecer.
14. Sin catálogo de tornillos (M2–M6) en la caja con tapa y la rosca.

## Usabilidad

Lo que se notó al usar la aplicación (de más a menos molesto):

| # | Observación | Propuesta |
|---|---|---|
| 1 | Los formularios de los generadores tienen muchos campos seguidos (el Lightbox, 14) y las ayudas están solo en tooltips | Agrupar con títulos («Medidas», «Tapa», «Cable»), ayuda visible bajo el campo en gris, y recordar los últimos valores |
| 2 | Al abrir la aplicación aparece la pregunta de recuperar una sesión anterior aunque no se haya perdido nada importante (la copia más reciente de un cierre inesperado) | Mostrarla solo una vez por copia, o con la fecha y el nombre y un «Descartar siempre» |
| 3 | La pestaña HERRAMIENTAS sigue visible dentro de un sketch con todo desactivado | Ocultarla mientras se dibuja, como SKETCH fuera del sketch |
| 4 | En la vista previa de un generador, lo ya existente se ve igual que lo nuevo | Mostrar lo generado en el color de «vista previa» (translúcido azul, como en las herramientas) |
| 5 | Mientras se prepara el dibujo de una rosca (segundo plano) no hay otra señal que un texto en la barra de estado | Un indicador pequeño sobre la vista («Preparando…») |
| 6 | Los errores de geometría salen en inglés técnico a veces («Error del kernel…») | Un diccionario de mensajes comprensibles para los más comunes |
| 7 | Las unidades no se pueden cambiar desde la barra de estado, solo en Archivo › Configuración | Un selector pequeño junto a las coordenadas del cursor |
| 8 | Sin atajos para las herramientas nuevas (Caja, Cilindro, generadores) | Asignar letras como a Extruir (E) y Redondeo (F) |
| 9 | El texto de ayuda de varios paneles menciona «mm» fijo (por ejemplo en la caja con tapa) | Usar la unidad elegida |

## Rendimiento medido (Debug, esta máquina)

| Qué | Antes | Ahora |
|---|---|---|
| Rosca M8×10 | 1,6–7,7 s | 0,03–0,06 s |
| Abrir la herramienta Rosca (visor incluido) | 28 s de parada | 0,1 s de interfaz libre; el dibujo llega en 0,3–1,3 s |
| Rosca de largo 60 | — | 5 s de preparación en segundo plano, sin pausas |
| Engranaje helicoidal 20 / 40 dientes con agujero | 2,7 / 7,2 s | 1,5 / 3,7 s |
| Unir o cortar con una rosca | ~2 s | ~2 s (en la booleana de OpenCASCADE) |
| Vista previa de los generadores | — | sin pausas apreciables en la app (no se midió con cronómetro); el par de engranajes algo más lento |

## Pruebas

196 casos, 1688 aserciones. Cubren el núcleo (sketch, solver, geometría, historial, archivos, comandos) y los generadores,
las unidades de las cotas, las primitivas, el mallado en segundo plano (a nivel de geometría) y las fórmulas del par de
engranajes. **No cubren** la interfaz de Qt ni OpenGL (ver «Abierto» nº 2).

## Orden recomendado

1. Hacer las pruebas manuales por fases y anotar lo que salga (lo que dé fallos entra como pendiente de su fase).
2. Poner la prueba automática de interfaz (nº 2), que protege todo lo demás.
3. Medir en Release (nº 3) y decidir si hace falta más optimización.
4. Las cotas ligadas a parámetros (nº 5) antes de ampliar generadores.
5. Partir los archivos grandes (nº 4) cuando haya prueba de interfaz que los proteja.
