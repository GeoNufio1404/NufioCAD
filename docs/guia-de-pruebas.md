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
| 5.12 | **Cantidad ligada** | Crea el parámetro `n = 4` (Sólido › Parámetros); en el Patrón escribe `n` en «Cantidad» y Enter | El cuadro acepta la expresión y calcula 4 copias; el tooltip muestra el valor |
| 5.13 | Cambiar el parámetro | Cambia `n` a `6` en Parámetros | El patrón se rehace con 6 copias; Ctrl+Z vuelve a 4 |
| 5.14 | Resultado no entero | Escribe `n/2` o `n*0.4` en «Cantidad» | La cantidad se redondea al entero más cercano (mínimo 2) y no hay error |

**Fíjate en:** ¿las aristas elegidas se ven claramente? ¿El panel explica qué falta elegir? ¿Algún resultado queda «raro» (caras de más, huecos)? Lo pendiente de esta fase: ver «Pendiente de 5a–5e» en la hoja de ruta.

---

### Flecha de la medida que se edita (todas las herramientas con medidas)

| # | Haz esto | Debe pasar |
|---|---|---|
| F.1 | Sólido › **Caja**; haz clic en «Ancho» (o entra con el teclado) | En la vista sale una flecha turquesa **de doble punta** a lo largo del ancho de la caja; al escribir otro valor la flecha se alarga con la vista previa |
| F.2 | Pasa a «Fondo», «Alto» y «Posición Z» | La flecha cambia a cada medida (fondo, alto, y la separación del plano en Z) |
| F.3 | Haz clic fuera del cuadro (por ejemplo en el vacío del panel o en la vista) | La flecha desaparece |
| F.4 | **Extruir** un perfil: haz clic en «Distancia», «Distancia 2» y «Desfase inicial» | Una flecha de doble punta por cada una, desde el perfil a lo largo de la normal (centrada si la dirección es Simétrica); sigue existiendo la flecha naranja para arrastrar |
| F.5 | **Cilindro** (diámetro y alto), **Plano** (distancia), **Mover cuerpo** (X/Y/Z), **Patrón** (distancia entre copias) y **Rosca** (diámetro y largo) | La flecha marca la medida que editas; en Mover y Patrón sale desde el centro del cuerpo |
| F.6 | Cambia de plano o elige una cara en Caja/Cilindro/Rosca | La flecha sigue al plano (gira con él) |
| F.8 | **Herramientas › Caja con tapa** (o Lightbox, Letrero): haz clic en Ancho, Fondo, Alto, Pared, Base, Grosor de la tapa… | La flecha turquesa marca esa medida sobre la pieza (el grosor de la pared en el borde derecho, la base en el fondo, etc.); en el Letrero solo el grosor de la placa |
| F.9 | **Ángulos**: Plano (ángulo, con un valor distinto de 0), Mover cuerpo (ángulo) y Patrón circular (ángulo total) | En vez de una flecha recta sale un **arco de doble punta** alrededor del eje de giro, con la apertura del ángulo |
| F.11 | **Revolución** con un ángulo (no vuelta completa): haz clic en «Ángulo» (y en «Ángulo 2» con «Dos lados») | Un arco de doble punta alrededor del eje, pasando por el perfil; con «Simétrica» el arco queda repartido a ambos lados del perfil |
| F.12 | **Extruir** con «Ángulo» (conicidad) distinto de 0, en el lado 1 y en el 2 | Un arco en la base de la extrusión entre la dirección recta y la pared inclinada |
| F.13 | **Unión**: elige dos caras y haz clic en «Distancia», «Ángulo» (Giratoria) y «Desplazamiento» (Deslizante) | Flechas sobre la cara fija: la distancia a lo largo de su normal, el desplazamiento a lo largo de su eje X y un arco alrededor de su normal |
| F.14 | **Engranaje**: haz clic en «Giro inicial», «Ángulo de presión» y «Ángulo de hélice» (con hélice distinta de 0) | Un arco alrededor del eje (giro inicial, desde el eje X), otro en el punto primitivo (presión) y otro entre el eje y el diente (hélice) |
| F.15 | **Caja**: radio de esquinas con un valor distinto de 0; **Rosca** con «Cortar»: «Holgura» | Una flecha del tamaño del radio desde la esquina; en la rosca, una flecha con el diámetro del agujero resultante (diámetro + 2 × holgura) |
| F.16 | **Generadores** (Caja con tapa, Lightbox, Letrero, Par de engranajes): haz clic en radio de esquinas, grosor del labio, diámetro de postes y agujeros (cierre con tornillos), margen, anilla y agujero de la anilla (llavero), módulo, dientes y agujeros del par | Cada medida tiene su flecha sobre la pieza: los postes y los agujeros en la esquina de arriba a la derecha, los agujeros del par en el centro de la rueda, el módulo y los dientes como Ø primitivo |
| F.10 | **Redondeo / Chaflán** (tamaño), **Vaciado** (espesor), **Agujero** (diámetro, profundidad, cabeza), **Engranaje** (módulo y dientes = diámetro primitivo, ancho, agujero), **Rosca** (paso) y **Posición X / Y** de Caja, Cilindro, Rosca y Engranaje | Cada medida tiene su flecha: el tamaño del redondeo desde la arista hacia dentro (solo aristas rectas), el espesor desde la cara abierta, el agujero en el primero de los círculos del sketch, X e Y desde el origen del plano |
| F.7 | Acepta o cancela la herramienta | No queda ninguna flecha en la vista |


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
| 6.17 | Con `alto` usado en una extrusión y en otro parámetro (`doble = alto*2`): Parámetros → renombrar `alto` a `espesor` → Aceptar | La fila `doble` pasa sola a `espesor*2` mientras escribes; la pieza no cambia ni queda en error; al reabrir la operación, su campo dice `espesor` |
| 6.18 | Ctrl+Z tras renombrar | Vuelven el nombre `alto` y la expresión de la operación; la pieza sigue igual |
| 6.19 | Renombrar a un nombre que ya existe | Aviso «repetido» y Aceptar desactivado; al corregirlo, las expresiones se actualizan con el nombre final |
| 6.20 | Parámetro `radio = 5`. Sketch con un círculo acotado (Ø o R) → doble clic en la cota → escribir `radio` (o `radio*2` en un Ø) → Aceptar → Terminar sketch | La medida es la del parámetro; el cuadro de la cota mostraba la expresión al reabrirla |
| 6.21 | Sólido › Parámetros → cambiar `radio` a `8` | El sketch y la pieza se recalculan (el círculo crece); Ctrl+Z lo devuelve |
| 6.22 | Poner `radio = -2` (o un valor que haga imposible el sketch) | La operación del sketch queda en «!» con el motivo («debe ser mayor que cero» o «no tiene solución»); al corregir, vuelve a la normalidad |
| 6.23 | Cota angular con `ang = 30` y expresión `ang` (o `ang + 15`) | Se entiende en grados; guardar y reabrir conserva la expresión |
| 6.24 | Renombrar `radio` a `r` en Parámetros | La cota sigue ligada (reabrirla muestra `r`) y la pieza no cambia |

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

**Fíjate en:** ¿se entiende qué cuerpo se mueve y cuál es la pieza fija y cuál la móvil en una unión? ¿Las caras elegidas se distinguen en la vista (hoy no se resaltan)? ¿Mover con números es suficiente o echas de menos arrastrar con el ratón? (ver «Pendiente de 8a–8c»).

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


### 9d. Sección

| # | Haz esto | Debe pasar |
|---|---|---|
| 9.20 | Con un cuerpo, Sólido › **Sección** | Aparece un panel con el plano, la posición y «Ver el otro lado»; la pieza se corta por la mitad y el corte se ve tapado en azul |
| 9.21 | Cambiar la posición | El corte se mueve a lo largo del eje |
| 9.22 | Cambiar de plano XY / XZ / YZ | El corte vuelve al centro de la pieza en ese eje |
| 9.23 | «Ver el otro lado» | Se ve la mitad contraria |
| 9.24 | Esc, o iniciar un sketch | La pieza vuelve entera; el modelo no cambió |

**Fíjate en:** ¿los avisos se entienden sin explicación? ¿Hay falsos avisos (te avisa de una pared que no existe)? ¿Echas de menos que señale dónde está el problema en la vista? ¿Los valores de tu impresora son los correctos (anota tu modelo)?

---

## Unidades y colocación

### Unidad en los cuadros numéricos

| # | Haz esto | Debe pasar |
|---|---|---|
| U.1 | Abre Sólido › Rosca y mira Diámetro, Paso y Largo | El «mm» sale como una etiqueta gris a la derecha del número, junto a las flechas; el número va solo |
| U.2 | Haz doble clic en el valor de un cuadro o pulsa Ctrl+A dentro | Solo se selecciona el número, no el «mm» |
| U.3 | Escribe `10` y Enter; luego `diametro*2` con un parámetro; luego `5 mm` | Las tres se aceptan |
| U.4 | Revisa Sección, Exportar/Imprimir (impresora) y la altura del texto del sketch | También con la unidad como etiqueta |

### Unidades de longitud

| # | Haz esto | Debe pasar |
|---|---|---|
| U.5 | Archivo › **Configuración…** y elige «Centímetros» | Al instante, los cuadros de las herramientas (con una abierta) muestran cm: M8 sale 0.800 cm |
| U.6 | Escribe `1` en un cuadro de longitud y Enter | Se entiende 1 cm (10 mm); escribe `5 mm` y se entiende 5 mm |
| U.7 | En un sketch, mira las cotas y el cursor (X/Y); pon una cota nueva | Todo en la unidad elegida; el cuadro de la cota dice «Valor (cm)» |
| U.8 | Cierra y abre la app | La unidad se recuerda |
| U.9 | Vuelve a «Milímetros» | Todo vuelve a mm; el archivo guardado no cambia |

### Rosca interior y colocación

| # | Haz esto | Debe pasar |
|---|---|---|
| R.1 | Con una pieza hecha, Sólido › Rosca y haz clic en una cara plana de la pieza | «Plano o cara» pasa a «Cara elegida» y X/Y se ponen en el centro de la cara; la rosca aparece sobre la cara |
| R.2 | Cambia Posición X o Y | La rosca se mueve en el plano de esa cara |
| R.3 | Cambia la operación a «Cortar (rosca interior)» | Aparece «Holgura»; el largo pasa a negativo y la rosca se corta hacia dentro de la pieza |
| R.4 | Sube la holgura (0,3 – 0,5) | El agujero sale más ancho (el texto dice el Ø del agujero) |
| R.5 | Haz un tornillo M8 con «Cuerpo nuevo» y pruébalo (impreso) en esa tuerca | Entra sin forzar con holgura ≈ 0,15–0,2 mm |
| R.6 | Haz lo mismo con Sólido › Engranaje (clic en una cara, X/Y, «Cortar») | Se coloca en la cara; con «Cortar» deja un hueco con la forma del engranaje |
| R.7 | Guarda, cierra y abre el diseño | Posición, cara y holgura se conservan |

---

## Fase 10 — Herramientas generadoras

### Herramientas generadoras (Lightbox)

| # | Haz esto | Debe pasar |
|---|---|---|
| G.1 | Pestaña HERRAMIENTAS (o menú Herramientas) › **Lightbox** | Se abre el formulario a la derecha y se ve una caja abierta por delante con su tapa (marco, labio y cara central) al lado |
| G.2 | Cambia Ancho, Alto, Pared… | La vista previa se actualiza (con «Calculando…» un momento) |
| G.3 | Cambia «Forma» a Redonda | Desaparecen Ancho y Alto, aparece Diámetro, y la caja es redonda |
| G.4 | Pon Ancho 20 y Pared 20 (un hueco imposible) | Avisa en rojo de la operación que falla y no deja aceptar, sin cerrar la herramienta |
| G.5 | Aceptar | Navegador: componente «Lightbox» con «Caja», «Marco», «Labio» y «Cara»; el historial muestra las 8 operaciones «Lightbox: …» |
| G.6 | Sólido › Parámetros | Aparecen ancho, alto, profundidad, pared, frente, tapa, labio, holgura y cable; al cambiar uno, todo se recalcula |
| G.7 | Ctrl+Z | Desaparece todo de una vez (operaciones, parámetros y componente) |
| G.8 | Genera un segundo Lightbox | Sus parámetros salen como ancho_2, alto_2… y no pisan los del primero |
| G.9 | Exporta o imprime los cuatro cuerpos | La revisión no avisa de paredes finas con los valores por defecto; marco, labio y cara no se solapan |
| G.10 | Sólido › Caja / Cilindro | Primitivas con posición, centradas o no, con alto ± y operación; doble clic en una operación «Lightbox: …» del historial abre su panel |
| G.11 | Compara con tus lightboxes de referencia (Illescas, Aguilar) | Las medidas son las de `docs/generadores.md`: 175 × 131 × 37 con pared 2; para igualar la de Illescas pon ancho 174,664, alto 132,036, profundidad 37 y holgura 0 |
| G.12 | Con el Lightbox ya aceptado, clic derecho en cualquiera de sus operaciones del historial › **Reabrir «Lightbox»…** | Se abre el formulario con las **mismas respuestas** con que se generó; la vista previa sustituye al diseño anterior (no se ve duplicado) |
| G.13 | Cambia solo medidas (Pared, Ancho…) y acepta | El diseño cambia **en su sitio**: las operaciones conservan su nombre y su orden, los cuerpos conservan sus nombres y el componente; los parámetros cambian de valor; Ctrl+Z lo devuelve todo |
| G.14 | Reabre y cambia algo que añade operaciones (por ejemplo «Agujero del cable» de 0 a 6) | Se rehace el grupo entero en el mismo sitio del historial; Ctrl+Z / Ctrl+Y funcionan |
| G.15 | Antes de reabrir, añade un Patrón o Redondeo sobre un cuerpo del generador y reabre cambiando algo que añade operaciones | Aviso en la barra de estado explicando que otra operación depende de las actuales; el formulario sigue abierto. Con cambios que no alteran la estructura sí se aplica |
| G.17 | Deja el ratón sobre un campo de un formulario de generador | Sale un tooltip con la explicación del campo (no hay texto fijo bajo el campo) |
| G.18 | Genera un Lightbox cambiando medidas, acepta; abre de nuevo Lightbox (o Caja con tapa, etc. con el suyo) | El formulario nuevo parte de las **últimas respuestas aceptadas** de ese generador; cada generador recuerda las suyas; al reabrir uno ya hecho (G.12) manda lo guardado en ese diseño |
| G.16 | Guarda, cierra y abre; reabre el generador | Sigue disponible con las respuestas de antes (también en el Letrero, el Par de engranajes y la Caja con tapa) |

### Caja con tapa

| # | Haz esto | Debe pasar |
|---|---|---|
| C.1 | Herramientas › **Caja con tapa** | Se ve una caja abierta por arriba y su tapa al lado; por defecto con labio |
| C.2 | Cambia «Cierre» a «Tornillos en las esquinas» | Aparecen cuatro postes con agujero en la caja y cuatro agujeros en la tapa; desaparecen los campos del labio y salen los de los postes |
| C.3 | Aceptar y abrir Sólido › Parámetros | Ancho, fondo, alto, pared… como parámetros; al cambiar «alto» la caja y los postes crecen |
| C.4 | Imprime la tapa con labio sobre la caja | Entra a presión sin forzar con holgura 0,2 (subir a 0,3 si queda dura) |
| C.5 | Prueba un tornillo M3 en un poste y en la tapa | Se enrosca en el poste (Ø2,5) y pasa libre por la tapa (Ø3,4) |

### Letrero o llavero

| # | Haz esto | Debe pasar |
|---|---|---|
| T.1 | Herramientas › **Letrero o llavero** | Se ve una placa con «Nufio» en relieve |
| T.2 | Escribe otro texto, cambia la fuente y el tamaño | Las letras y la placa se actualizan (la placa se ajusta al texto) |
| T.3 | Cambia «Tipo» a Llavero | Aparece la anilla con su agujero a la izquierda y los campos de la anilla |
| T.4 | Cambia «Texto» a Grabado | Las letras quedan hundidas en la placa (un solo cuerpo) |
| T.5 | Prueba letras con hueco (O, A, B, 8) | El hueco queda libre, no se rellena |
| T.6 | Aceptar | Navegador: «Placa» y «Texto» en el componente; el historial tiene el sketch «…: letras» y sus operaciones |
| T.7 | Imprime con relieve en otro color | El cuerpo «Texto» se asigna a otro filamento sin tocar la placa |

### Par de engranajes

| # | Haz esto | Debe pasar |
|---|---|---|
| E.1 | Herramientas › **Par de engranajes** | Se ven dos ruedas (12 y 24 dientes) con los dientes encajados, sin tocarse; el panel dice la relación 12:24 y la distancia 36,15 mm |
| E.2 | Cambia los dientes (por ejemplo 15 y 30) y el módulo | Las ruedas se recolocan solas a la nueva distancia y siguen encajadas |
| E.3 | Pon un ángulo de hélice de 15° | Las dos ruedas llevan la hélice en sentido contrario |
| E.4 | Aceptar y abrir Sólido › Parámetros | `modulo`, `dientes_1`, `dientes_2`… y `giro_rueda_2` (calculado); al cambiar un parámetro, todo se recalcula |
| E.5 | Imprime las dos ruedas con sus ejes | Giran juntas sin atascarse con el juego de 0,15 mm (subirlo a 0,25 si rozan) |
| E.6 | Haz doble clic en «Engranaje: rueda 2» del historial | Se abre su panel con Posición X, Giro inicial y los demás campos ligados |

---

## Cálculo en segundo plano

La app ya no se cuelga mientras recalcula: lo lento (una rosca cortante, un engranaje, una booleana grande) corre en un hilo aparte.

| # | Haz esto | Debe pasar |
|---|---|---|
| B.1 | Crea un **cilindro de 10 mm**, pon una **rosca** con operación «Cortar» encima de él y acepta | Al aceptar la ventana sigue respondiendo (puedes girar la vista); la barra de estado dice «Calculando el modelo…» y la rosca aparece al terminar |
| B.2 | Haz doble clic en el cilindro y pon **20 mm de diámetro**; acepta | La ventana no se congela; mientras calcula sigues viendo la pieza anterior; al terminar se ve el cilindro de 20 mm con su rosca |
| B.3 | Vuelve a editar el cilindro a **10 mm** y, sin esperar, cambia a 15 y a 12 | Solo vale el último: acaba en 12 mm; no se acumulan cálculos ni se cierra la app |
| B.4 | Mientras calcula (barra de estado «Calculando…») intenta abrir una herramienta o exportar | Avisa «El modelo todavía se está calculando; espera a que termine» y no hace nada; al terminar sí abre |
| B.5 | Mientras calcula pulsa **Ctrl+Z** | Se cancela lo que calculaba y se calcula el estado anterior (la ventana no se traba) |
| B.6 | Abre la herramienta **Rosca** (o **Engranaje** con muchos dientes) y cambia el diámetro varias veces seguidas | La ventana no se congela; aparece «Calculando…» si tarda; «Aceptar» se activa cuando la vista previa está lista; la vista previa final es la del último valor |
| B.7 | Abre un diseño grande (o uno con roscas) desde Archivo › Abrir | La ventana aparece enseguida; el modelo y el mensaje «Abierto …» salen al terminar de calcular |
| B.8 | Cierra la app mientras calcula | Se cierra limpia, sin error ni proceso colgado en el administrador de tareas |

---

## Errores de geometría en lenguaje claro

| # | Haz esto | Debe pasar |
|---|---|---|
| M.1 | **Redondeo** de una arista de una caja con un radio mayor que la caja | El panel (en rojo) dice «No se pudo redondear: prueba con un radio menor» (este y el de vaciado ya eran claros) |
| M.2 | **Vaciado** con un espesor mayor que la mitad de la pieza | «No se pudo vaciar: prueba con un espesor menor» |
| M.3 | Intenta provocar un fallo del kernel: **Unir** o **Cortar** dos cuerpos que solo se toquen por una arista, un **Barrido** con el perfil lejos del inicio de la trayectoria, o un **Agujero** enorme | Si el kernel falla, el mensaje empieza por «No se pudo …», dice qué probar y deja el detalle técnico al final entre paréntesis; ya no sale «Error del kernel…». Estos casos son difíciles de provocar (casi siempre una validación lo avisa antes), así que anótame si ves algún mensaje que siga siendo oscuro |
| M.4 | Mira el historial después de un error | La operación aparece con ⛔ y el mismo mensaje al pasar el ratón |

---

## Prueba automática y fuentes

### Prueba automática de la interfaz

No hace falta tocar nada: abre la ventana real, recorre las herramientas y los generadores y escribe un informe.

```
NufioCAD.exe --prueba-interfaz [--informe ruta.txt] [--umbral-ms 20000]
```

- Abre cada generador (Lightbox, Caja con tapa, Letrero o llavero, Par de engranajes), cambia un valor, acepta y deshace; hace lo mismo con Rosca, Engranaje, Caja y Cilindro; cancela una herramienta; cambia las unidades a cm y vuelve a mm; activa y desactiva Sección y Medir con un cuerpo; cambia el diámetro de un cilindro con una rosca cortante encima (debe volver al instante y calcular en segundo plano); y comprueba que al final no queda nada.
- Falla si algo no se abre o no se cierra, si deshacer deja restos, o si la interfaz se para más del umbral (por defecto 20 s: en Debug OpenCASCADE es lento).
- El informe (por defecto `%TEMP%\NufioCAD-prueba-interfaz.txt`) dice la parada más larga de cada paso y qué la provocó, y, si hubo paradas de más de un segundo, **dónde estaba el programa** (la pila de llamadas).
- Usa ajustes y carpetas propios (no toca tus ajustes, tus recientes ni las copias de recuperación). Devuelve 0 si todo va bien.

### Letrero con fuentes decorativas

1. Generadores → Letrero o llavero, texto «Nufio», fuente **Ravie** (o Jokerman, Curlz, etc.).
2. Debe aparecer la placa con las letras; si algo falla, el panel muestra el motivo en rojo (nunca en blanco).

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
