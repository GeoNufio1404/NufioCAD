# Guía de pruebas manuales — NufioCAD

Para ir probando fase por fase cuando haya tiempo. Cada prueba dice **qué hacer** y **qué debería pasar**; si algo no es así (o no te gusta cómo se siente), anótalo con la plantilla del final y mándamelo.

> Las pruebas automáticas (172 casos) ya cubren la geometría, el archivo y el historial. Lo que esta guía busca es lo que no pueden ver: **si la interfaz es clara, si se siente fluida y si los resultados se ven bien**.

## Antes de empezar

- **Abrir el programa** (Windows, Debug): ejecutar `build\windows-debug\src\app\NufioCAD.exe` (hay que tener Qt en el PATH: `C:\Qt\6.8.3\msvc2022_64\bin`). Si lo compilas tú: `cmake --build build\windows-debug`.
- **Pieza de ejemplo**: si quieres empezar con un cuerpo hecho, Archivo › Abrir y elige cualquier `.nuf` tuyo, o crea una caja: Crear sketch → plano XY → Rectángulo (R) → escribe `40` Tab `25` Enter → Terminar sketch → clic dentro del rectángulo → **E** → escribe `10` Enter.
- **Si se cierra solo**: al volver a abrir te ofrece restaurar. Cuéntame qué hacías justo antes (el programa guarda un informe del cierre en `%APPDATA%\NufioCAD\NufioCAD\cierres`; no hace falta que me lo mandes, yo puedo leerlo).
- **Rendimiento**: esta build es *Debug* (más lenta que la final). Anota igualmente qué se siente lento, pero tenlo en cuenta (las roscas y los engranajes helicoidales son lo más pesado).

---

## Fase 1 — Visor y navegación

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 1.1 | Arrastrar con el botón izquierdo en el vacío | Orbita alrededor del modelo |
| 1.2 | Arrastrar con el botón derecho (o el central) | Desplaza (pan) |
| 1.3 | Rueda del ratón | Zoom hacia donde apunta el cursor |
| 1.4 | Clic en una cara y en una arista; pasar el cursor | La cara/arista se resalta al pasar y se marca al hacer clic; la barra inferior dice qué hay seleccionado |
| 1.5 | Shift + clic, y Shift + arrastrar | Agrega/quita de la selección; el rectángulo selecciona varios |
| 1.6 | ViewCube (arriba a la derecha): clic en una cara, arista o esquina | La cámara gira a esa vista |
| 1.7 | Barra flotante inferior: inicio, ajustar, y las herramientas Orbitar / Desplazar / Zoom | Cada una hace lo suyo; «ajustar» encuadra todo |

**Fíjate en:** ¿la órbita se siente natural (como Fusion)? ¿El zoom a veces «salta»? ¿Se entiende qué está seleccionado?

---

## Interfaz general (transversal)

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| I.1 | Abrir el programa | Lienzo vacío con cinta (pestaña SÓLIDO), navegador a la izquierda, timeline abajo |
| I.2 | Pasar el cursor por los botones de la cinta | Tooltip con nombre y descripción |
| I.3 | Ver › tema claro/oscuro (si está en el menú) | Los iconos y colores cambian en caliente |
| I.4 | Archivo › Recientes… (Ctrl+Mayús+O) | Lista con miniaturas; Esc la cierra |
| I.5 | Con una herramienta activa (p. ej. Extruir) | El panel de Propiedades aparece a la derecha; sin herramienta se oculta |

**Fíjate en:** ¿encuentras las cosas sin buscarlas? ¿Algún botón no se entiende por su icono? ¿Falta algo que esperabas ver (lo pendiente está en la hoja de ruta, sección «Interfaz estilo Fusion 360»)?

---

## Fase 2 — Sketcher 2D

Las pruebas **1–19** ya están escritas con detalle en `docs/hoja-de-ruta-cad.md` (secciones «Pruebas manuales pendientes (2a y 2b)» y «(2c y 2d)»). Resumen de lo que cubren:

- Dibujo: línea, rectángulo, círculo, arco, polígono (con el cuadro de lados), elipse, ranura, spline, cónica, texto, imagen de referencia, importar DXF.
- Edición: recortar, extender, partir, empalme, chaflán, desfase, simetría, mover/copiar/girar/escalar, patrones.
- Restricciones y cotas (doble clic en una cota para cambiarla), medidas visibles, proyectar aristas.

Pruebas añadidas desde entonces:

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 2.A | En un sketch, dibujar varias líneas y pasar el cursor por una | Solo se ven las medidas y restricciones de lo que tiene el cursor o está seleccionado (Ver › «Mostrar todas…» las muestra todas) |
| 2.B | Polígono: `8` Tab `30` Enter | Octógono de 30 mm; el cuadro de valores se puede escribir sin que se cierre |
| 2.C | Dibujar un sketch con muchas figuras (50 o más) y arrastrar un punto | Debe seguir fluido (anota si se traba) |

**Fíjate en:** ¿el cursor y los valores escritos junto al dibujo son cómodos? ¿Se entiende cuándo una figura está «totalmente restringida»? ¿Hay textos que se tapan?

---

## Fase 3 — Modelado básico y timeline

Las pruebas **20–30** están en la hoja de ruta («3a y 3b», «3c»). Cubren: extruir (unir/cortar/simétrica/dos lados/a través de todo/hasta una cara/ángulo/desfase), empujar una cara, revolución, editar con doble clic en el timeline, deshacer/rehacer, mover el marcador, suprimir/renombrar/borrar/reordenar operaciones, navegador con casillas, proyección vinculada y avisos.

**Fíjate en:** ¿editar una operación pasada es claro? ¿Los avisos (`!`) explican bien el problema? ¿El timeline se entiende?

---

## Fase 4 — Archivos y exportación

Las pruebas **31–38** están en la hoja de ruta («Pruebas manuales al terminar (Fase 4)»): exportar STL/STEP, guardar y reabrir, cambios sin guardar, recuperación, importar un STEP real, recientes. Añado:

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 4.A | Archivo › Nuevo con cambios sin guardar | Pregunta si guardar |
| 4.B | Guardar como… con otro nombre, y luego abrirlo | Mismo modelo; el título de la ventana muestra el nombre |
| 4.C | Abrir un `.nuf` con doble clic desde el explorador (si lo asociaste) o arrastrándolo | Se abre (la asociación puede no estar hecha: anótalo) |
| 4.D | Exportar con «un archivo por cuerpo» | Un archivo por cada cuerpo elegido |

**Fíjate en:** ¿los mensajes de error al abrir un archivo malo son comprensibles? ¿Alguna vez pierdes trabajo?

---

## Fase 5 — Operaciones de acabado

Necesitas un cuerpo (la caja de 40×25×10 de «Antes de empezar»). Los botones están en el grupo **MODIFICAR** de la cinta y quedan desactivados si no hay cuerpos.

| # | Operación | Qué hacer | Qué debería pasar |
|---|---|---|---|
| 5.1 | **Redondeo** (F) | Clic en una arista (se marca azul); más aristas con clics sucesivos; escribe el radio `3` | Vista previa en vivo; las aristas elegidas se redondean; Aceptar crea «Redondeo1» |
| 5.2 | Redondeo con radio imposible | Radio `500` | Aviso en rojo en el panel, sin romper nada; Aceptar desactivado |
| 5.3 | **Chaflán** | Igual con la distancia `2` | Cortes planos en las aristas |
| 5.4 | Editar el redondeo | Doble clic en su ficha del timeline; cambiar el radio | El modelo se recalcula; las operaciones posteriores se mantienen |
| 5.5 | **Vaciado** | Elegir la cara superior; espesor `2` | Cuerpo hueco abierto por arriba; probar «Hacia fuera» |
| 5.6 | **Agujero** | Crear sketch en la cara superior, dibujar 2 círculos, Terminar; Sólido › Agujero (H); elegir el sketch; diámetro `5`, «Atravesar todo» | Dos agujeros pasantes; probar Abocardado y Avellanado (aparecen sus campos) |
| 5.7 | **Espejo** | Sólido › Espejo; plano YZ; «Unir» | Cuerpo simétrico de 80 mm de ancho; con «Cuerpo nuevo» queda una copia aparte |
| 5.8 | Espejo sobre una cara | Elegir «Cara plana elegida» y clic en una cara lateral | Se refleja sobre esa cara |
| 5.9 | **Patrón lineal** | Sólido › Patrón; Lineal, eje X, cantidad `3`, distancia `60` | Tres copias separadas; con distancia `20` se funden en una pieza |
| 5.10 | **Patrón circular** | Circular, eje Z, cantidad `4`, 360° | Cuatro copias alrededor del eje Z |
| 5.11 | Editar la pieza original después | Cambiar la altura de la extrusión | Redondeos, agujeros, espejo y patrón siguen a la pieza |

**Fíjate en:** ¿las aristas elegidas se ven claramente? ¿El panel explica qué falta elegir? ¿Algún resultado queda «raro» (caras de más, huecos)? Lo pendiente de esta fase: ver «Pendiente de 5a–5e» en la hoja de ruta.

---

## Fase 6 — Geometría de referencia y parámetros

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 6.1 | Sólido › **Plano** (grupo CONSTRUIR); base «Plano XY», distancia `25` | Cuadrado turquesa a 25 mm; Aceptar → «Plano1» en el timeline y en el navegador (carpeta «Planos de construcción») |
| 6.2 | Crear sketch → clic en el plano turquesa | «Sketch en Plano1»: dibuja y extruye; el cuerpo nace a 25 mm de altura |
| 6.3 | Editar Plano1 (doble clic): distancia `40` | Todo lo que se apoya en él sube |
| 6.4 | Plano con ángulo: ángulo `45`, girar alrededor del «Eje X del plano» | Plano inclinado |
| 6.5 | Casilla del plano en el navegador | Lo oculta y lo muestra |
| 6.6 | **Espejo** con un plano de construcción | El plano aparece en la lista «Plano de simetría» |
| 6.7 | Sólido › **Eje**; primer plano XY, segundo XZ | Línea turquesa a lo largo del eje X; «Ninguno» da la perpendicular al primero |
| 6.8 | Revolución y Patrón circular con ese eje | El eje aparece en las listas de «Eje» |
| 6.9 | Sólido › **Parámetros**; Añadir; nombre `alto`, expresión `3*4` | Valor `12` al lado; nombre repetido o inválido → aviso rojo y Aceptar desactivado |
| 6.10 | En **Extruir** (o Plano, Redondeo…) escribir en «Distancia» `alto*2` y Enter | Se calcula `24`; el tooltip del cuadro muestra el valor |
| 6.11 | Volver a Parámetros y cambiar `alto` a `20` | La pieza se recalcula (distancia 40) |
| 6.12 | Escribir `sin(30)*10`, `2 in`, `1,5 + alto` | `5`, `50.8`, `21.5` |
| 6.13 | Referencia circular: `a = b+1`, `b = a+1` | Aviso «Referencia circular» |
| 6.14 | Borrar un parámetro que se usa | La operación pasa a error con un mensaje claro |
| 6.15 | Guardar y reabrir | Parámetros y expresiones siguen; cambiar un parámetro sigue recalculando |
| 6.16 | Ctrl+Z tras cambiar parámetros | Vuelve al valor anterior |

**Fíjate en:** ¿se entiende que un cuadro «tiene una expresión»? ¿Escribir un nombre de parámetro es natural? ¿Falta algo (las cotas del sketch aún no se ligan; ver «Pendiente de 6c»)?

---

## Fase 7 — Generadores

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 7.1 | Sólido › **Engranaje** (grupo GENERAR); valores por defecto | Engranaje de 20 dientes, módulo 2, Ø primitivo 40 mm, con agujero de 5 mm |
| 7.2 | Cambiar dientes a `12`, `40`, `100` | Se rehace cada vez; con muy pocos dientes (< 6) o ángulos raros, mensaje claro |
| 7.3 | Ángulo de hélice `20` | Dientes inclinados (helicoidal); es más lento |
| 7.4 | Aceptar y mirar de cerca | Flancos lisos, sin dientes «rotos»; probar el volumen con Archivo › Exportar STL en un slicer |
| 7.5 | Dos engranajes del mismo módulo: crear uno, mover el otro con un plano de construcción desplazado | Los dientes deberían poder encajar (distancia entre ejes = módulo × (z1+z2)/2) |
| 7.6 | Sólido › **Rosca**; medida M8 × 1.25, largo `10` | Varilla roscada; el panel dice el diámetro menor (6.47 mm) |
| 7.7 | Cambiar «Medida» a M6; luego tocar el diámetro a mano | La medida pasa a «Personalizada» |
| 7.8 | «Rosca a izquierdas» | La hélice gira al otro lado |
| 7.9 | **Rosca interior**: caja, agujero de Ø 6.47 con la herramienta Agujero, y rosca M8 con operación «Cortar» | El agujero queda roscado |
| 7.10 | Sólido › **Barrido**: sketch A (perfil, un cuadrado en el plano XZ centrado en el origen), sketch B (trayectoria: una línea y otra en L en el plano XY desde el origen); elegir el cuadrado y las líneas | Barra cuadrada que sigue la L; prueba también «Fija» |
| 7.11 | Barrido con una trayectoria rota (dos líneas sin tocarse) | Mensaje de error claro |
| 7.12 | Sólido › **Loft**: cuadrado de 10 en XY y otro de 4 en un plano de construcción a 20 mm; elegirlos **en ese orden** | Tronco de pirámide; probar «Suave» y «Recto» y tres perfiles |

**Fíjate en:** ¿el cálculo de engranajes/roscas tarda demasiado (anota cuánto)? ¿La elección del perfil y la trayectoria en el barrido/loft es clara o confusa? ¿Los textos del panel guían? Esto es lo menos probado en la interfaz (ver «Pendiente de 7c/7d»).

---

## Fase 8 — Multi-cuerpo y ensambles

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 8.1 | Con un cuerpo, Sólido › **Mover cuerpo** (grupo MODIFICAR); Mover en X `60` | Vista previa del cuerpo desplazado; Aceptar → ficha «Mover1» |
| 8.2 | Ángulo `45` con el eje Z «Centro del cuerpo» y luego «Origen» | Gira sobre sí mismo o alrededor del origen |
| 8.3 | Marcar «Crear una copia» | El original se queda y aparece «Cuerpo2» |
| 8.4 | Con dos cuerpos, clic en una cara del segundo mientras Mover está abierto | El desplegable «Cuerpo» cambia a ese cuerpo |
| 8.5 | Sólido › **Combinar cuerpos**; destino Cuerpo1, marcar Cuerpo2, Unir (que se toquen o solapen) | Un solo cuerpo; con «Cortar» queda el destino con el hueco; «Intersecar» deja solo lo común |
| 8.6 | Combinar con «Conservar los cuerpos herramienta» | Los dos cuerpos siguen en el navegador |
| 8.7 | Editar la pieza original (cambiar la altura de su extrusión) | Mover y Combinar se rehacen sobre ella |

**Fíjate en:** ¿se entiende qué cuerpo se mueve? ¿Mover con números es suficiente o echas de menos arrastrar con el ratón? (ver «Pendiente de 8a»).

## Fase 9

Aún no existe; cuando se implemente agregaré sus pruebas aquí.

---

## Fase de pruebas transversal (cuando tengas tiempo)

1. **Flujo completo**: diseña algo tuyo (por ejemplo una carcasa con tapa y tornillos) usando varias fases a la vez; guarda, cierra, abre, edita algo del principio del historial.
2. **Estrés**: sketches con muchas figuras, piezas con 10+ operaciones, patrones de 20+ copias. Anota qué se siente lento.
3. **Errores**: intenta romper cosas (radios enormes, borrar cosas de las que dependen otras, abrir un archivo vacío renombrado a `.nuf`).
4. **Impresión 3D**: exporta STL de varias piezas y ábrelas en tu slicer (¿cerradas, en mm, sin caras invertidas?).
5. **Claridad**: ¿qué cosa te costó descubrir sin ayuda? Eso es feedback de UX muy valioso.

## Plantilla para darme feedback

```
Fase / prueba:      (por ejemplo 5.6 Agujero)
Qué hice:
Qué esperaba:
Qué pasó:
Tipo:               [ ] error  [ ] se ve mal  [ ] confuso / poco intuitivo  [ ] lento  [ ] falta algo  [ ] me gusta
Gravedad:           [ ] bloquea  [ ] molesta  [ ] detalle
Captura / archivo .nuf (opcional):
```

Con una captura de pantalla y los pasos suele bastar. Si algo se cierra, no hace falta que copies nada: el informe queda guardado y lo leo yo.
