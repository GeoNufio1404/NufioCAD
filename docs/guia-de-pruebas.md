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

| 8.8 | Con dos cuerpos, Sólido › **Componentes** (grupo ORGANIZAR): ponle nombre a cada uno (`Base`, `Tapa`) y escribe `Carcasa` como componente de los dos | En el navegador aparece la carpeta «Carcasa» con los dos cuerpos con sus nombres nuevos |
| 8.9 | Casilla de «Carcasa» en el navegador | Oculta y muestra los dos cuerpos a la vez |
| 8.10 | Guardar, cerrar y abrir; luego Ctrl+Z tras cambiar nombres | Nombres y componentes se conservan; deshacer los revierte |
| 8.11 | Borrar el texto de un nombre | El cuerpo recupera su nombre de fábrica (Cuerpo1) |

| 8.12 | Dos cajas separadas (una con **Mover** → «Crear una copia»). Sólido › **Unión** (grupo ORGANIZAR). Clic en la cara **superior** de la primera y luego en la cara **inferior** de la segunda (hay que girar la vista para ver el fondo) | El panel dice «Fija: … — Móvil: …» y la segunda caja se apoya sobre la primera, centrada |
| 8.13 | Distancia `5` | Quedan separadas 5 mm |
| 8.14 | Tipo «Giratoria» con ángulo `90` | La segunda gira sobre la normal de la cara |
| 8.15 | Tipo «Deslizante» con desplazamiento `10` | Se corre 10 mm sobre la cara |
| 8.16 | Botón «Intercambiar fija y móvil» | Ahora se mueve la otra caja |
| 8.17 | Clic en una cara ya elegida | La quita de la elección |
| 8.18 | Elegir dos caras del mismo cuerpo | Error claro en el panel |
| 8.19 | Cambiar la altura de la caja fija y volver a la Unión | La caja móvil sube con ella |

**Fíjate en:** ¿las caras elegidas se distinguen en la vista (hoy no se resaltan)? ¿Se entiende cuál es la fija y cuál la móvil?

**Fíjate en:** ¿se entiende qué cuerpo se mueve? ¿Mover con números es suficiente o echas de menos arrastrar con el ratón? (ver «Pendiente de 8a»).

## Fase 9 — Pulido para impresión 3D

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| 9.1 | Archivo › **Exportar** con una pieza normal, formato STL | Debajo del resumen de triángulos aparece «✓ Todo en orden para imprimir con tu impresora» |
| 9.2 | Hacer una extrusión de `0.3` mm de grosor y exportar | Aviso naranja «Pared de 0.30 mm … más fina que tu boquilla … no se imprimirá» |
| 9.3 | Grosor `0.6` | Aviso de «menos de dos líneas de boquilla … saldrá débil» |
| 9.4 | Vaciar una caja con espesor `0.3` y exportar | Aviso de pared fina en esa pieza |
| 9.5 | Dos cuerpos que se solapan (copia desplazada 15 mm de una caja de 20) | Aviso «Cuerpo1 y Cuerpo2 se solapan (… mm³)»; si los separas, desaparece |
| 9.6 | En «Mi impresora», poner la cama en `10 × 256 × 256` | Aviso «no cabe en tu cama» (el resumen se actualiza al cambiar el número); al volver a `256` desaparece |
| 9.7 | Cerrar el diálogo y abrirlo de nuevo, o reiniciar el programa | La boquilla y la cama se recuerdan |
| 9.8 | Cambiar el formato a STEP | La revisión y «Mi impresora» se ocultan (no es malla) |
| 9.9 | Pieza con muchos detalles (engranaje de 60 dientes) | La revisión no se queda colgada (puede tardar un poco) |

| 9.10 | Archivo › **Imprimir…** (Ctrl+P) | Diálogo «Imprimir» con la misma revisión; el botón «Imprimir» está desactivado y dice «Elige el programa de tu slicer» |
| 9.11 | «Elegir…» y señalar el `.exe` de tu slicer (Bambu Studio, Orca…) | El botón se activa; se recuerda la próxima vez |
| 9.12 | Pulsar «Imprimir» | Tu slicer se abre con la pieza ya cargada (el STL queda en `%TEMP%\NufioCAD`) |
| 9.13 | Imprimir con un cuerpo oculto desmarcado | Solo se manda lo marcado |


### 9c. Medir

| # | Haz esto | Debe pasar |
|---|---|---|
| 9.14 | Con un cuerpo, SÓLIDO › INSPECCIONAR › **Medir** | Aparece un panel arriba a la izquierda de la vista: «Elige una cara o una arista» |
| 9.15 | Clic en una cara plana | Área y normal |
| 9.16 | Clic en una arista recta; luego en el borde de un agujero | Longitud; radio, diámetro y centro |
| 9.17 | Clic en dos caras paralelas | Distancia mínima y «Paralelas, separadas X mm» |
| 9.18 | Clic en dos agujeros del mismo tipo | «Distancia entre ejes/centros» |
| 9.19 | Esc, o iniciar un sketch o una herramienta | El panel desaparece y Medir se apaga |

**Fíjate en:** ¿los avisos se entienden sin explicación? ¿Hay falsos avisos (te avisa de una pared que no existe)? ¿Echas de menos que señale dónde está el problema en la vista? ¿Los valores de tu impresora son los correctos (anota tu modelo)?

Medición, análisis de sección y el botón «Imprimir» (abrir el slicer): se agregan aquí cuando se implementen.

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
