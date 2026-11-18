# Guía de pruebas manuales — NufioCAD

Para ir probando sección por sección cuando haya tiempo. Cada prueba dice **qué hacer** y **qué debería pasar**; si algo no es así (o no te gusta cómo se siente), anótalo con la plantilla del final y mándamelo.

> Las pruebas automáticas (unos 250 casos del núcleo y ~36 pasos de la prueba de interfaz) ya cubren la geometría, el archivo y el historial. Lo que esta guía busca es lo que no pueden ver: **si la interfaz es clara, si se siente fluida y si los resultados se ven bien**.

## Antes de empezar

- **Abrir el programa** (Windows, Debug): ejecutar `build\windows-debug\src\app\NufioCAD.exe` (hay que tener Qt en el PATH: `C:\Qt\6.8.3\msvc2022_64\bin`). Si lo compilas tú: `cmake --build build\windows-debug`.
- **Pieza de ejemplo**: si quieres empezar con un cuerpo hecho, Archivo › Abrir y elige cualquier `.nuf` tuyo, o crea una caja: Crear sketch → plano XY → Rectángulo (R) → escribe `40` Tab `25` Enter → Terminar sketch → clic dentro del rectángulo → **E** → escribe `10` Enter.
- **Si se cierra solo**: al volver a abrir te ofrece restaurar. Cuéntame qué hacías justo antes (el programa guarda un informe del cierre en `%APPDATA%\NufioCAD\NufioCAD\cierres`; no hace falta que me lo mandes, yo puedo leerlo).
- **Rendimiento**: esta build es *Debug* (más lenta que la final). Anota igualmente qué se siente lento, pero tenlo en cuenta (las roscas y los engranajes helicoidales son lo más pesado).

---

## Cómo está organizada esta guía

Va en el mismo orden que el programa: **1. General**, **2. Sketch**, **3. Sólido** y **4. Herramientas**. Cada subsección tiene un **código** de letras y sus pruebas se numeran seguidas con ese código (por ejemplo `SKC.12`); el código y el número son únicos en toda la guía, así que buscar un ID lleva a una sola prueba. Al final hay una tabla con los **IDs anteriores** y su equivalencia.

| Sección | Código |
|---|---|
| 1.1 Visor y navegación | `NAV` |
| 1.2 Interfaz general | `UI` |
| 1.2.1 Ayudas (tooltips) y barra de estado | `AYU` |
| 1.2.2 Temas claro y oscuro | `TEM` |
| 1.3 Unidades | `UNI` |
| 1.4 Cálculo en segundo plano | `CAL` |
| 1.5 Vista previa translúcida y «Calculando…» | `VPR` |
| 1.6 Errores de geometría en lenguaje claro | `ERR` |
| 1.7 Archivo: guardar, abrir, exportar e imprimir | `ARC` |
| 2.1 Dibujo y edición | `SKD` |
| 2.2 Figuras abiertas (editar la figura recién dibujada) | `SKF` |
| 2.3 Inferencias y guías | `SKI` |
| 2.4 Cotas | `SKC` |
| 2.5 Restricciones e iconos | `SKR` |
| 2.6 Rendimiento del sketch | `SKP` |
| 3.1 Extruir y modelado básico | `EXT` |
| 3.2 Operaciones de acabado (modificar) | `ACA` |
| 3.3 Flecha de la medida que se edita (todas las herramientas con medidas) | `FLE` |
| 3.4 Mover y girar arrastrando (estilo Orca) | `MOV` |
| 3.5 Geometría de referencia y parámetros | `REF` |
| 3.6 Generadores de piezas (engranaje, rosca, barrido y loft) | `GEN` |
| 3.7 Multi-cuerpo y ensambles | `MUL` |
| 3.8 Inspeccionar: medir y sección | `INS` |
| 4.1 Lightbox | `LIG` |
| 4.2 Caja con tapa | `CAJ` |
| 4.3 Letrero o llavero | `LET` |
| 4.4 Par de engranajes | `PAR` |
| 4.5 Todos los generadores: reabrir, recordar y restablecer | `GRE` |
| 4.6 Familias de herramientas y largo de la cremallera | `CRE` |
| 5.4 Vigilante de paradas (`NUF_VIGILANTE`) | `VIG` |
| 5.5 Llamadas en segundo plano (exportar, importar, medir, recuperar) | `FLU` |
| 5.6 Dibujo en segundo plano (mallas, vista previa, generadores) | `FLU` |
| 5.7 Indicador de carga y cierre con trabajo en marcha | `FLU` |
| 5.8 Calcular menos (patrones, nombres, refresco y arrastre) | `FLU` |
| 5.9 Memoria al exportar | `FLU` |

---

## 1. General (interfaz, vista y archivos)

### 1.1 Visor y navegación

Código de las pruebas: **NAV**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| NAV.1 | Arrastrar con el botón izquierdo en el vacío | Orbita alrededor del modelo |
| NAV.2 | Arrastrar con el botón derecho (o el central) | Desplaza (pan) |
| NAV.3 | Rueda del ratón | Zoom hacia donde apunta el cursor |
| NAV.4 | Clic en una cara y en una arista; pasar el cursor | La cara/arista se resalta al pasar y se marca al hacer clic; la barra inferior dice qué hay seleccionado |
| NAV.5 | Shift + clic, y Shift + arrastrar | Agrega/quita de la selección; el rectángulo selecciona varios |
| NAV.6 | ViewCube (arriba a la derecha): clic en una cara, arista o esquina | La cámara gira a esa vista |
| NAV.7 | Barra flotante inferior: inicio, ajustar, y las herramientas Orbitar / Desplazar / Zoom | Cada una hace lo suyo; «ajustar» encuadra todo |

**Fíjate en:** ¿la órbita se siente natural (como Fusion)? ¿El zoom a veces «salta»? ¿Se entiende qué está seleccionado?

### 1.2 Interfaz general

Código de las pruebas: **UI**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| UI.1 | Abrir el programa | Lienzo vacío con cinta (pestaña SÓLIDO), navegador a la izquierda, timeline abajo |
| UI.2 | Pasar el cursor por los botones de la cinta | Tooltip con nombre y descripción |
| UI.3 | Ver › tema claro/oscuro (si está en el menú) | Los iconos y colores cambian en caliente |
| UI.4 | Archivo › Recientes… (Ctrl+Mayús+O) | Lista con miniaturas; Esc la cierra |
| UI.5 | Con una herramienta activa (p. ej. Extruir) | El panel de Propiedades aparece a la derecha; sin herramienta se oculta |

**Fíjate en:** ¿encuentras las cosas sin buscarlas? ¿Algún botón no se entiende por su icono? ¿Falta algo que esperabas ver (lo pendiente está en la hoja de ruta, sección «Interfaz estilo Fusion 360»)?

#### 1.2.1 Ayudas (tooltips) y barra de estado

Código: **AYU**. Estado: fusionado, falta probar a mano.

| ID | Haz esto | Debe pasar |
|---|---|---|
| AYU.1 | Deja el ratón sobre **cada herramienta** de la barra SKETCH (y las de los desplegables) | Aparece su nombre, su tecla y una línea que dice qué hace |
| AYU.2 | Abre los **menús** (Sketch, Restricciones, Archivo, Editar) y deja el ratón sobre una entrada | También se ve la ayuda |
| AYU.3 | Activa una herramienta y cámbiala por otra | La **barra de abajo es siempre un banner azul** (no aparece ni desaparece, ni sin mensaje); al cambiar el mensaje **parpadea** un momento (se aclara y vuelve al azul) y la indicación sale en negrita |
| AYU.4 | Mira el banner con el tema claro y el oscuro | Se lee bien en ambos |

#### 1.2.2 Temas claro y oscuro

Código: **TEM**. Estado: en prueba. Cambia el tema en Ver › Tema.

| ID | Haz esto | Debe pasar |
|---|---|---|
| TEM.1 | Tema **claro**: mira la ventana con una pieza y un panel abierto (por ejemplo Caja) | Cada zona se distingue por su tono: el marco de la ventana (gris azulado), la cinta (casi blanca, elevada), el navegador y los campos (blancos), y los paneles tienen su barra de título en negrita |
| TEM.2 | Tema claro: lee el «mm» de los cuadros numéricos, los rótulos de la cinta y los textos apagados | Se leen sin esfuerzo (contraste suficiente) |
| TEM.3 | Tema **oscuro**: mira las casillas de verificación (Navegador y paneles) marcadas y sin marcar | Se ve su borde aunque no estén marcadas; marcadas salen en azul con la marca blanca |
| TEM.4 | Cambia entre claro, oscuro y sistema varias veces | Cambia al instante, sin parpadeos raros ni textos ilegibles, y se recuerda al reabrir |
| TEM.5 | En ambos temas, abre un menú (Sólido) y un cuadro de diálogo (Exportar) | Los menús y los diálogos se ven coherentes con el tema |
| TEM.6 | Con el tema claro y el oscuro, mueve y gira la vista con una pieza grande | Sigue igual de fluido que antes (los cambios son de color, no afectan al rendimiento) |
| TEM.7 | Tema claro: abre Exportar con una pieza que dé avisos (por ejemplo una pared de 0,3 mm) y Parámetros con una expresión errónea (`ancho +`) | El verde de «todo en orden», el ámbar de los avisos y el rojo de los errores se leen bien sobre el fondo gris, sin esfuerzo |
| TEM.8 | Tema claro: entra a un sketch con varias figuras y restricciones y activa «Mostrar todas» | Las medidas, iconos y trazos se leen bien (nada de gris muy claro sobre claro) |
| TEM.9 | Con un sketch abierto, cambia de tema (Ver › Tema) | El dibujo cambia de colores al instante (trazos, medidas e iconos), sin tener que salir y volver a entrar |

### 1.3 Unidades

Código: **UNI**.

#### 1.3.1 Unidad en los cuadros numéricos

| ID | Haz esto | Debe pasar |
|---|---|---|
| UNI.1 | Abre Sólido › Rosca y mira Diámetro, Paso y Largo | El «mm» sale como una etiqueta gris a la derecha del número, junto a las flechas; el número va solo |
| UNI.2 | Haz doble clic en el valor de un cuadro o pulsa Ctrl+A dentro | Solo se selecciona el número, no el «mm» |
| UNI.3 | Escribe `10` y Enter; luego `diametro*2` con un parámetro; luego `5 mm` | Las tres se aceptan |
| UNI.4 | Revisa Sección, Exportar/Imprimir (impresora) y la altura del texto del sketch | También con la unidad como etiqueta |

#### 1.3.2 Unidades de longitud

| ID | Haz esto | Debe pasar |
|---|---|---|
| UNI.5 | Archivo › **Configuración…** y elige «Centímetros» | Al instante, los cuadros de las herramientas (con una abierta) muestran cm: M8 sale 0.800 cm |
| UNI.6 | Escribe `1` en un cuadro de longitud y Enter | Se entiende 1 cm (10 mm); escribe `5 mm` y se entiende 5 mm |
| UNI.7 | En un sketch, mira las cotas y el cursor (X/Y); pon una cota nueva | Todo en la unidad elegida; el cuadro de la cota dice «Valor (cm)» |
| UNI.8 | Cierra y abre la app | La unidad se recuerda |
| UNI.9 | Vuelve a «Milímetros» | Todo vuelve a mm; el archivo guardado no cambia |

### 1.4 Cálculo en segundo plano

Código: **CAL**.

La app ya no se cuelga mientras recalcula: lo lento (una rosca cortante, un engranaje, una booleana grande) corre en un hilo aparte.

| ID | Haz esto | Debe pasar |
|---|---|---|
| CAL.1 | Crea un **cilindro de 10 mm**, pon una **rosca** con operación «Cortar» encima de él y acepta | Al aceptar la ventana sigue respondiendo (puedes girar la vista); la barra de estado dice «Calculando el modelo…» y la rosca aparece al terminar |
| CAL.2 | Haz doble clic en el cilindro y pon **20 mm de diámetro**; acepta | La ventana no se congela; mientras calcula sigues viendo la pieza anterior; al terminar se ve el cilindro de 20 mm con su rosca |
| CAL.3 | Vuelve a editar el cilindro a **10 mm** y, sin esperar, cambia a 15 y a 12 | Solo vale el último: acaba en 12 mm; no se acumulan cálculos ni se cierra la app |
| CAL.4 | Mientras calcula (barra de estado «Calculando…») intenta abrir una herramienta o exportar | Avisa «El modelo todavía se está calculando; espera a que termine» y no hace nada; al terminar sí abre |
| CAL.5 | Mientras calcula pulsa **Ctrl+Z** | Se cancela lo que calculaba y se calcula el estado anterior (la ventana no se traba) |
| CAL.6 | Abre la herramienta **Rosca** (o **Engranaje** con muchos dientes) y cambia el diámetro varias veces seguidas | La ventana no se congela; aparece «Calculando…» si tarda; «Aceptar» se activa cuando la vista previa está lista; la vista previa final es la del último valor |
| CAL.7 | Abre un diseño grande (o uno con roscas) desde Archivo › Abrir | La ventana aparece enseguida; el modelo y el mensaje «Abierto …» salen al terminar de calcular |
| CAL.8 | Cierra la app mientras calcula | Se cierra limpia, sin error ni proceso colgado en el administrador de tareas |

### 1.5 Vista previa translúcida y «Calculando…»

Código: **VPR**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| VPR.1 | Herramientas › **Lightbox** (o cualquier generador) | La pieza de la vista previa se ve **azul y translúcida**, distinta de lo que ya hay en el diseño (gris) |
| VPR.2 | Cambia un valor del formulario | Sale «⏳ Calculando…» abajo a la izquierda de la vista si tarda, y desaparece al terminar |
| VPR.3 | Acepta el generador | Las piezas pasan a verse grises y opacas, como cualquier cuerpo |
| VPR.4 | Reabre el generador (clic derecho › Reabrir) | Lo anterior se oculta y se ve la vista previa azul en su lugar |
| VPR.5 | Cambia el diámetro de un cilindro con una rosca encima | Mientras recalcula sale «Calculando…» sobre la vista y desaparece al terminar |

### 1.6 Errores de geometría en lenguaje claro

Código: **ERR**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| ERR.1 | **Redondeo** de una arista de una caja con un radio mayor que la caja | El panel (en rojo) dice «No se pudo redondear: prueba con un radio menor» (este y el de vaciado ya eran claros) |
| ERR.2 | **Vaciado** con un espesor mayor que la mitad de la pieza | «No se pudo vaciar: prueba con un espesor menor» |
| ERR.3 | Intenta provocar un fallo del kernel: **Unir** o **Cortar** dos cuerpos que solo se toquen por una arista, un **Barrido** con el perfil lejos del inicio de la trayectoria, o un **Agujero** enorme | Si el kernel falla, el mensaje empieza por «No se pudo …», dice qué probar y deja el detalle técnico al final entre paréntesis; ya no sale «Error del kernel…». Estos casos son difíciles de provocar (casi siempre una validación lo avisa antes), así que anótame si ves algún mensaje que siga siendo oscuro |
| ERR.4 | Mira el historial después de un error | La operación aparece con ⛔ y el mismo mensaje al pasar el ratón |

### 1.7 Archivo: guardar, abrir, exportar e imprimir

Código: **ARC**. Las pruebas **31–38** están en la hoja de ruta («Pruebas manuales al terminar (Fase 4)»): exportar STL/STEP, guardar y reabrir, cambios sin guardar, recuperación, importar un STEP real, recientes.

#### 1.7.1 Guardar, abrir y exportar

| ID | Haz esto | Debe pasar |
|---|---|---|
| ARC.1 | Archivo › Nuevo con cambios sin guardar | Pregunta si guardar |
| ARC.2 | Guardar como… con otro nombre, y luego abrirlo | Mismo modelo; el título de la ventana muestra el nombre |
| ARC.3 | Abrir un `.nuf` con doble clic desde el explorador (si lo asociaste) o arrastrándolo | Se abre (la asociación puede no estar hecha: anótalo) |
| ARC.4 | Exportar con «un archivo por cuerpo» | Un archivo por cada cuerpo elegido |
| ARC.5 | Guarda como `Prueba2.nuf`, ábrelo en otra sesión y también «Guardar como…» | El título de la ventana muestra el nombre del archivo |

**Fíjate en:** ¿los mensajes de error al abrir un archivo malo son comprensibles? ¿Alguna vez pierdes trabajo?

#### 1.7.2 Revisión para imprimir (Exportar)

| ID | Haz esto | Debe pasar |
|---|---|---|
| ARC.6 | Archivo › **Exportar** con una pieza normal, formato STL | Debajo del resumen de triángulos aparece «✓ Todo en orden para imprimir con tu impresora» |
| ARC.7 | Hacer una extrusión de `0.3` mm de grosor y exportar | Aviso naranja «Pared de 0.30 mm … más fina que tu boquilla … no se imprimirá» |
| ARC.8 | Grosor `0.6` | Aviso de «menos de dos líneas de boquilla … saldrá débil» |
| ARC.9 | Vaciar una caja con espesor `0.3` y exportar | Aviso de pared fina en esa pieza |
| ARC.10 | Dos cuerpos que se solapan (copia desplazada 15 mm de una caja de 20) | Aviso «Cuerpo1 y Cuerpo2 se solapan (… mm³)»; si los separas, desaparece |
| ARC.11 | En «Mi impresora», poner la cama en `10 × 256 × 256` | Aviso «no cabe en tu cama» (el resumen se actualiza al cambiar el número); al volver a `256` desaparece |
| ARC.12 | Cerrar el diálogo y abrirlo de nuevo, o reiniciar el programa | La boquilla y la cama se recuerdan |
| ARC.13 | Cambiar el formato a STEP | La revisión y «Mi impresora» se ocultan (no es malla) |
| ARC.14 | Pieza con muchos detalles (engranaje de 60 dientes) | La revisión no se queda colgada (puede tardar un poco) |

#### 1.7.3 Imprimir (slicer)

| ID | Haz esto | Debe pasar |
|---|---|---|
| ARC.15 | Archivo › **Imprimir…** (Ctrl+P) | Diálogo «Imprimir» con la misma revisión; el botón «Imprimir» está desactivado y dice «Elige el programa de tu slicer» |
| ARC.16 | «Elegir…» y señalar el `.exe` de tu slicer (Bambu Studio, Orca…) | El botón se activa; se recuerda la próxima vez |
| ARC.17 | Pulsar «Imprimir» | Tu slicer se abre con la pieza ya cargada (el STL queda en `%TEMP%\NufioCAD`) |
| ARC.18 | Imprimir con un cuerpo oculto desmarcado | Solo se manda lo marcado |

**Fíjate en:** ¿los avisos se entienden sin explicación? ¿Hay falsos avisos (te avisa de una pared que no existe)? ¿Echas de menos que señale dónde está el problema en la vista? ¿Los valores de tu impresora son los correctos (anota tu modelo)?

---

### 1.8 Interfaz en cualquier pantalla (`RSP`, Fase 20)

Código: **RSP**. Conviene probarlo en tu pantalla pequeña **y** en la de diario. Para emular una pantalla más chica en la de diario: Configuración › Pantalla › Escala de Windows (125 %, 150 %, 200 %).

| ID | Haz esto | Debe pasar |
|---|---|---|
| RSP.1 | Sólido › **Engranaje** (o cualquier herramienta) | El panel de Propiedades se abre con todos sus campos; si no caben en alto, aparece una barra de desplazamiento y **Cancelar y Aceptar quedan siempre visibles abajo** |
| RSP.2 | Con el panel abierto, desplaza con la rueda del ratón sobre el panel | Los campos suben y bajan; los botones de abajo no se mueven |
| RSP.3 | Herramientas › **Par de engranajes** (también «Engranaje interno» y «Cremallera») | El panel se abre más ancho que los demás si hay sitio; ya no se cortan los campos por los lados (si la ventana es muy estrecha, sale una barra horizontal) |
| RSP.4 | Reduce la ventana al mínimo con una herramienta abierta | La ventana ya no crece sola ni se sale de la pantalla por culpa del panel |
| RSP.5 | Escribe en un campo y pulsa Tab hasta pasar el último | El panel se desplaza solo hasta mostrar el campo con el foco |
| RSP.6 | Acepta y cancela varias herramientas seguidas | Sin parpadeos ni paneles vacíos; Enter acepta y Esc cancela como antes |
| RSP.7 | Estrecha la ventana poco a poco (de 1500 px hacia 900 px de ancho) | Primero los botones de la cinta pierden su nombre y quedan solo los iconos; si aún no caben, los últimos grupos pasan al botón **«Más »»** (arriba a la derecha, junto a las pestañas) |
| RSP.8 | Abre «Más »» y elige una herramienta (por ejemplo Rosca o Medir) | Aparecen los grupos escondidos, con su nombre, y la herramienta se abre como si hubieras pulsado su botón; las familias (por ejemplo, Redondeo/Chaflán) salen como submenús |
| RSP.9 | Agranda la ventana otra vez | Los nombres y los grupos vuelven a verse en la cinta y desaparece «Más »» |
| RSP.10 | Con la ventana de menos de 1100 px de ancho | El Navegador se pliega solo; con Ver › Paneles vuelve a abrirse; al agrandar la ventana, vuelve solo si se había plegado solo |
| RSP.11 | Con la ventana de menos de 640 px de alto | La línea de tiempo se pliega sola y la cinta pierde los nombres de los grupos; al agrandar vuelve todo. Cierra el Navegador a mano, estrecha y agranda la ventana: **no** se vuelve a abrir solo |
| RSP.12 | Con escala de pantalla de 150 % o 200 %: abre un sketch con cotas y mira el cubo de vistas | El cubo, sus letras y los textos de las cotas tienen un tamaño proporcionado al resto de la interfaz (ni diminutos ni desbordados) |
| RSP.13 | Abre Archivo › Exportar, Sólido › Parámetros y el diálogo de Cuerpos; cambia el tamaño de uno, ciérralo y ábrelo otra vez | Ninguno es más grande que la pantalla; Exportar, Parámetros y Cuerpos se reabren con el tamaño en que los dejaste |
| RSP.14 | Cierra el programa con el Navegador plegado solo (ventana angosta) y ábrelo con la ventana grande | El Navegador aparece (no se guardó como cerrado) |

**Matriz resolución × escala** (marca lo que probaste: «ventana principal», «una herramienta», «un generador ancho (Par de engranajes)», «sketch con cotas»):

| Pantalla lógica | Escala de Windows | Ventana | Herramienta | Generador | Sketch |
|---|---|---|---|---|---|
| 1920×1080 | 100 % | [ ] | [ ] | [ ] | [ ] |
| 1536×864 | 125 % | [ ] | [ ] | [ ] | [ ] |
| 1280×720 | 150 % | [ ] | [ ] | [ ] | [ ] |
| 960×540 | 200 % | [ ] | [ ] | [ ] | [ ] |
| 1366×768 | 100 % | [ ] | [ ] | [ ] | [ ] |
| 1024×768 | 100 % | [ ] | [ ] | [ ] | [ ] |
| Otra (la tuya pequeña) | | [ ] | [ ] | [ ] | [ ] |

Para repetirlo sin tocar Windows: `NufioCAD --inventario <carpeta> --tamano 1280x680 --escala 1.5` y mirar `inventario.txt` y las capturas.

## 2. Sketch

### 2.1 Dibujo y edición

Código: **SKD**.

Las pruebas **1–19** ya están escritas con detalle en `docs/hoja-de-ruta.md` (secciones «Pruebas manuales pendientes (2a y 2b)» y «(2c y 2d)»). Resumen de lo que cubren:

- Dibujo: línea, rectángulo, círculo, arco, polígono (con el cuadro de lados), elipse, ranura, spline, cónica, texto, imagen de referencia, importar DXF.
- Edición: recortar, extender, partir, empalme, chaflán, desfase, simetría, mover/copiar/girar/escalar, patrones.
- Restricciones y cotas (doble clic en una cota para cambiarla), medidas visibles, proyectar aristas.

Pruebas añadidas desde entonces:

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKD.1 | En un sketch, dibujar varias líneas y pasar el cursor por una | Solo se ven las medidas y restricciones de lo que tiene el cursor o está seleccionado (Ver › «Mostrar todas…» las muestra todas) |
| SKD.2 | Polígono: `8` Tab `30` Enter | Octógono de 30 mm; el cuadro de valores se puede escribir sin que se cierre |
| SKD.3 | Dibujar un sketch con muchas figuras (50 o más) y arrastrar un punto | Debe seguir fluido (anota si se traba) |
| SKD.4 | Entra a un sketch con el fondo **gris** y activa **Línea** | La cruz del cursor es blanca con borde oscuro y se ve en cualquier fondo |
| SKD.5 | **Anota** lo que siga mal (captura) | Son los pendientes |

**Fíjate en:** ¿el cursor y los valores escritos junto al dibujo son cómodos? ¿Se entiende cuándo una figura está «totalmente restringida»? ¿Hay textos que se tapan?

### 2.2 Figuras abiertas (editar la figura recién dibujada)

Código: **SKF**. Estado: en prueba.

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKF.1 | **Círculo**: clic en el centro y clic en el borde | El círculo queda dibujado y el recuadro sigue enseñando **Diámetro** con su valor |
| SKF.2 | Escribe otro diámetro y Enter | El mismo círculo cambia de tamaño (no aparece otro) y queda cotado |
| SKF.3 | **Rectángulo** (esquinas y por centro): dos clics; escribe solo el **alto** y Enter | Cambia el alto, el ancho se conserva |
| SKF.4 | **Polígono** (los tres modos): dos clics; escribe **8** en Lados y Enter; luego otro tamaño | Pasa a 8 lados y luego cambia de tamaño, sin figuras repetidas |
| SKF.5 | Con una figura abierta, haz **clic** en otro sitio | La figura se queda y empieza otra |
| SKF.6 | Con una figura abierta pulsa **Esc**; luego otra vez **Esc** | El primero cierra la edición (la figura se queda); el segundo sale de la herramienta |
| SKF.7 | Crea una figura, cámbiala y pulsa **Deshacer** | Vuelve al paso anterior; si sigues escribiendo no pasa nada raro |
| SKF.8 | **Círculo por 2 y 3 puntos**, **rectángulo por 3 puntos** y **ranura**: termina la figura y escribe un valor (diámetro, longitud/alto, ancho) | Cambia la misma figura; con Deshacer vuelve a antes de crearla (un solo paso) |
| SKF.9 | **Elipse** (no el arco): dibuja una y, sin hacer clic, escribe los semiejes mayor y menor | Cambia la misma elipse (también si solo escribes uno); con Deshacer vuelve a antes de crearla |
| SKF.10 | **Línea y un círculo o arco lejano:** dibuja un círculo, déjate sobre él un instante (sale un anillo en su centro) y empieza una línea lejos | Al apuntar hacia el centro sale ⊥ y la línea se endereza a esa recta; al apuntar a una tangente sale T. Al hacer clic se crea la restricción (pasa por el centro / tangente) |
| SKF.11 | **Punto medio despierto:** déjate sobre una línea un instante y dibuja otra línea hacia la altura de su punto medio | Se pega a la horizontal o vertical del punto medio con la guía naranja (solo alinea: no crea restricción) |
| SKF.12 | **Cotas juntas:** pon tres cotas lineales en segmentos paralelos muy juntos | Ningún texto cae sobre la línea de cota de otra; si hace falta, el texto se corre a lo largo de su línea |
| SKF.13 | **Anota** qué te falta o molesta | Son los pendientes |

### 2.3 Inferencias y guías

Código: **SKI**. Estado: en prueba. Aquí están las guías naranjas, los puntos «despertados», perpendicular, paralela y tangente.

#### 2.3.1 Alineación horizontal/vertical y guías naranjas

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKI.1 | Abre un sketch, elige **Línea** y mueve el cursor hacia la derecha, a la altura del origen (sin hacer clic) | El cursor se **pega** a la altura del origen y aparece una **línea discontinua naranja** del origen al cursor; con el cursor a más de ~6 px de esa altura la guía desaparece |
| SKI.2 | Haz lo mismo hacia arriba, sobre la vertical del origen | Igual, con la guía vertical |
| SKI.3 | Haz clic estando pegado a la horizontal del origen | La línea empieza ahí; en el sketch hay una restricción **horizontal entre ese punto y el origen** (selecciona el punto: se ve el icono H) |
| SKI.4 | Dibuja una línea; con **Línea** o **Rectángulo** pasa el cursor sobre su extremo (se marca amarillo) y aléjate hacia la derecha a su misma altura | Aparece la guía naranja desde ese extremo (el punto quedó «despierto»); un clic crea la restricción con él |
| SKI.5 | Despierta dos puntos distintos y lleva el cursor a la altura de uno y la vertical del otro | El cursor se pega al **cruce** y salen **dos guías** |
| SKI.6 | Mantén **Mayús** mientras mueves el cursor | No hay alineaciones ni guías (como en Onshape); al soltarla vuelven |
| SKI.7 | Mantén **Ctrl** | Dibujo libre, sin guías (como antes) |
| SKI.8 | Cerca de un punto existente (a menos de 8 px) | Gana el punto: el cursor se pega a él, sin guía |
| SKI.9 | Línea cuyo inicio ya está a la altura del origen y que acabas **horizontal** | Una sola restricción horizontal en la línea; el sketch no marca restricciones redundantes ni en conflicto |
| SKI.10 | Deshacer (Ctrl+Z) tras un clic con guía; guardar y reabrir | Desaparece la línea y su restricción; el archivo guarda las restricciones como cualquier otra |
| SKI.11 | **Anota** qué te falta o molesta (sobre todo: si la guía aparece demasiado, si «tiembla» al pegarse y se suelta, si querías ver un anillo en el punto despierto) | Una nota por caso: son los pendientes de la entrega |
| SKI.12 | Dibuja una línea inclinada y un punto aparte; con **Línea**, lleva el cursor **hacia la línea** hasta quedar a la altura del punto | Sale la guía naranja discontinua y el cursor se pega a la línea en esa altura; al clic, queda sobre la línea y alineado |
| SKI.13 | Igual con un **eje** (X o Y) y con un círculo | También sale la guía y se pega |
| SKI.14 | Pasa el cursor por el **punto medio** de una línea | Punto amarillo y **cuadrado naranja discontinuo** a su alrededor, bien visible |
| SKI.15 | Lleva el cursor despacio hasta la guía naranja horizontal de un punto y sepárate poco a poco | La guía se queda pegada hasta unos 10 px y luego se suelta, sin parpadear en el borde |

#### 2.3.2 Puntos «despertados»

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKI.16 | Con **Línea**, pasa el cursor **rápido** por un punto y sigue; luego quédate **medio segundo** sobre otro | El primero no despierta; el segundo muestra un **anillo amarillo** y desde lejos se alinea con él |
| SKI.17 | Aleja el cursor del todo y vuelve a la herramienta | El anillo solo se ve con una herramienta de dibujo activa |
| SKI.18 | **Anota** qué te falta o molesta | Son los pendientes |

#### 2.3.3 Perpendicular y paralela

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKI.19 | Con **Línea**, dibuja un primer tramo inclinado (por ejemplo a 30°) y, desde su final, lleva el cursor casi a **90°** de él (a menos de 3°) | El segundo tramo se endereza a 90°, el primer tramo se resalta en **naranja discontinuo** y aparece el símbolo **⊥** en medio del tramo; al clic, la restricción **perpendicular** (icono ⊥ en el sketch) |
| SKI.20 | Igual, pero llevando el cursor casi en **la misma dirección** que el primer tramo | Se endereza, se resalta el primero y sale **∥**; al clic, restricción **paralela** |
| SKI.21 | Con un ángulo claramente distinto (45°, 60°…) | Sin símbolo ni guía, el tramo sigue al cursor |
| SKI.22 | Empieza una línea nueva desde el **extremo de otra** línea que ya existe y haz lo de SKI.19 | También infiere con esa línea |
| SKI.23 | Cerca de la horizontal o la vertical, o cerca de un punto existente, o con **Mayús/Ctrl** pulsada | Mandan la horizontal/vertical y los puntos antes que ⊥/∥; Mayús y Ctrl las suprimen |
| SKI.24 | Dibuja una línea inclinada. Con «Rectángulo 3 puntos» empieza en su extremo y lleva el primer lado casi perpendicular (o paralelo) | Se endereza, la línea se resalta en naranja y sale ⊥ (∥); termina el rectángulo y el sketch queda sin conflictos |

#### 2.3.4 Tangente

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKI.25 | Dibuja un arco; con **Línea**, empieza en uno de sus extremos y lleva el cursor casi en la dirección en que seguiría el arco (a menos de 3°) | El tramo se endereza a la tangente y sale una **T**; al clic, restricción **tangente** (icono T) |
| SKI.26 | Lo mismo yendo hacia atrás (hacia el arco) o con un ángulo claro | Sin T; el tramo sigue al cursor |
| SKI.27 | Dibuja una línea; con «Arco» empieza en su extremo, marca el final y pasa el tercer punto cerca de la curva que seguiría la línea | La curva se endereza, sale «T» y el arco queda tangente a la línea (sin conflicto al arrastrar) |
| SKI.28 | Con **Línea**, deja el cursor medio segundo sobre una línea inclinada que esté lejos (sale un anillo amarillo en su punto medio); luego dibuja un tramo nuevo desde otro sitio, casi paralelo o perpendicular a ella (a menos de 3°) | El tramo se endereza, la línea lejana se resalta en naranja y sale ∥ o ⊥; al clic, la restricción queda con esa línea (sin conflictos al arrastrar) |
| SKI.29 | Lo mismo con **Rectángulo de 3 puntos** (su primer lado) y sin haber dejado el cursor sobre ninguna línea | Con la línea despierta se endereza; sin ella no hay inferencia |

### 2.4 Cotas

Código: **SKC**. Estado: en prueba.

#### 2.4.1 Cota de un clic sobre el lienzo (estilo Onshape)

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKC.1 | En un sketch, dibuja un **círculo con dos clics** (centro y un punto del borde), sin escribir nada | Queda dibujado sin cota |
| SKC.2 | Pulsa **D** (o el botón Cota) sin nada seleccionado | La barra de estado dice «Cota: clic en un círculo, arco o línea…»; al pasar el cursor se resalta lo que se acotaría |
| SKC.3 | Haz clic en el círculo | Sale un **campo pequeño junto al cursor** («Ø 20.000 mm») con el valor actual seleccionado. Escribe 30 y Enter: el diámetro pasa a 30 y se ve la cota «Ø30» |
| SKC.4 | Escribe una expresión con un parámetro (por ejemplo `grosor * 2`) | Se acepta igual que antes (si el parámetro existe) |
| SKC.5 | Con el modo Cota activo, haz clic en la cota «Ø30» | Se reabre el campo con su valor; cámbialo y Enter |
| SKC.6 | Abre el campo y pulsa Esc, o haz clic fuera | No cambia nada (el clic fuera solo cierra el campo) |
| SKC.7 | Dibuja **tres círculos con el mismo centro** y radios parecidos y acótalos con un clic cada uno | Las cotas salen en **ángulos distintos** (cada una 30° más que la anterior) y los textos no se pisan |
| SKC.8 | Haz clic en una **línea**, y luego (con la primera elegida) en otra línea | La primera abre el campo de su longitud (Esc para no acotarla); con dos líneas elegidas sale el ángulo o la distancia entre ellas |
| SKC.9 | Haz clic en **dos puntos** (o un punto y una línea) | Sale el campo de la distancia entre ellos |
| SKC.10 | Pulsa Esc con algo elegido, y otra vez Esc | La primera quita lo elegido; la segunda sale del modo Cota |
| SKC.11 | Selecciona una línea normalmente y pulsa D | Funciona como antes (acota lo seleccionado), ahora con el campo sobre el lienzo |
| SKC.12 | Deshacer (Ctrl+Z) tras acotar | Quita la cota |
| SKC.13 | Abre el campo de una cota y, sin cerrarlo, haz zoom o gira la vista | Anota qué pasa: el campo se queda donde estaba (sabido: no sigue a la vista); que no se cierre el programa ni se pierda el valor |
| SKC.14 | **Lo de antes sigue igual:** con la herramienta Círculo, marca el centro y **escribe** el diámetro antes del segundo clic (y lo mismo con Línea: longitud y ángulo) | Se crea la figura con su cota, como siempre |
| SKC.15 | Guarda, cierra, abre el diseño y edita el sketch | Las cotas siguen ahí con su valor; un clic en una (modo Cota) o doble clic (sin herramienta) la reabre con el campo |
| SKC.16 | **Anota dónde se pisan las cotas** (esto es para la siguiente entrega, no es un fallo de esta): un rectángulo pequeño con sus cuatro cotas, dos líneas paralelas a 1–2 mm acotadas, una cota junto a un icono de restricción (H, V…), y acercando y alejando la vista | Una captura de cada caso en que dos textos o un texto y un icono se tapen; servirán de casos de prueba para «cotas sin solaparse» |

**Antes de fusionar esta rama:** pasar SKC.1–SKC.16 en Windows (y, cuando se pueda, en Fedora). Lo que falle se anota con la plantilla del final; después viene la auditoría de código del sketch (ver la hoja de ruta, «Sketch al estilo Onshape»).

#### 2.4.2 Cotas que no se pisan

Usa los casos que anotaste en **SKC.16**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKC.17 | Dibuja un **rectángulo pequeño** (unos 6 × 4 mm) y acota sus cuatro lados | Los cuatro números se leen sin taparse (los que chocarían se alejan un renglón de la geometría) |
| SKC.18 | Dibuja **dos líneas paralelas a 1–2 mm** y acota cada una | Los dos números no se pisan |
| SKC.19 | Una línea con restricción **horizontal** (icono H) y su cota | El icono y el número no se pisan |
| SKC.20 | **Diez círculos con el mismo centro** y acótalos | Cada diámetro en un ángulo distinto, sin tapar a otro |
| SKC.21 | Con cualquiera de los casos anteriores, **acerca y aleja** la vista con la rueda | Siempre se ven sin pisarse (las distancias son en píxeles); las cotas no «bailan» al mover el cursor |
| SKC.22 | Acota un **ángulo** entre dos líneas cerca de otra cota | El arco del ángulo se aleja si su texto chocaría |
| SKC.23 | Un sketch grande (por ejemplo 15–20 cotas) | Se sigue moviendo con fluidez |
| SKC.24 | **Anota** dónde aún se pisa algo (un texto con una línea del dibujo, con un punto o con la línea de otra cota) | Una captura por caso: son los pendientes (esas colisiones aún no se evitan) |
| SKC.25 | Acota **5–6 círculos** juntos (distintos centros y concéntricos) | Los textos y las líneas de cota no se pisan entre sí |
| SKC.26 | Acota ángulos de 90° y otros (dibujando las líneas en ambos sentidos: llegando al vértice y saliendo de él) | El arco y el número están en la **esquina** de las dos líneas, dentro del ángulo que se mide |
| SKC.27 | Con una cota o un icono cuyo sitio natural cae sobre otra línea del dibujo | El texto se aparta de esa línea si hay hueco |

| SKC.30 | Dibuja muchas líneas paralelas y muy juntas (unas 20 a 0,3 mm) y acótalas todas | Las cotas que no caben en ningún renglón sacan su texto a un hueco libre con una **línea de llamada** hasta su cota; ningún número queda encima de otro |

#### 2.4.3 Cotas estables y cota seleccionada

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKC.28 | Dibuja dos líneas paralelas muy juntas y acótalas; arrastra un extremo | Las cotas no saltan de sitio mientras arrastras |
| SKC.29 | Selecciona la cota que quedó apartada | Pasa a su sitio natural y la otra se aparta; ninguna se pisa |

### 2.5 Restricciones e iconos

Código: **SKR**. Estado: en prueba.

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKR.1 | Aplica una restricción con la selección (por ejemplo **Horizontal** sobre una línea) y luego otra distinta sobre otra línea, sin activar «Mostrar todas las restricciones» | El icono de cada restricción que aplicaste **se queda a la vista** aunque no pases el cursor ni la selecciones, para saber en cuáles ya pusiste restricción y en cuáles no; si la deshaces (Ctrl+Z) o la borras, su icono desaparece |
| SKR.2 | Pon restricciones horizontal, vertical, paralela, perpendicular, tangente, igual, concéntrica, punto medio, simetría y fija; muestra las restricciones | Cada una lleva su icono dibujado (no letras) y se distingue a simple vista |
| SKR.3 | Pasa el cursor y selecciona un icono | Cambia de color y se puede seleccionar con clic como antes |
| SKR.4 | Dibuja una línea casi perpendicular/paralela a otra, y un arco casi tangente | La insignia ⊥ / ∥ / T naranja se ve nítida junto al cursor |
| SKR.5 | Acerca y aleja la vista | Los iconos mantienen su tamaño en pantalla |

### 2.6 Rendimiento del sketch

Código: **SKP**. Estado: en prueba.

Rama `feature/auditoria-sketch`. Hace falta un sketch grande: dibuja una **rejilla de unas 20 × 20 casillas** (por ejemplo, un rectángulo con patrón rectangular, o importa un DXF grande).

| ID | Haz esto | Debe pasar |
|---|---|---|
| SKP.1 | Con el sketch grande y **sin herramienta**, pasa el cursor por encima | El resaltado sigue al cursor sin tirones |
| SKP.2 | Termina el sketch y extruye el perfil | Los perfiles salen sin esperas largas |
| SKP.3 | Acerca y aleja la vista con muchas cotas en pantalla | Las cotas se recolocan con fluidez |

---

## 3. Sólido

### 3.1 Extruir y modelado básico

Código: **EXT**. Las pruebas **20–30** están en la hoja de ruta («3a y 3b», «3c»). Cubren: extruir (unir/cortar/simétrica/dos lados/a través de todo/hasta una cara/ángulo/desfase), empujar una cara, revolución, editar con doble clic en el timeline, deshacer/rehacer, mover el marcador, suprimir/renombrar/borrar/reordenar operaciones, navegador con casillas, proyección vinculada y avisos.

**Fíjate en:** ¿editar una operación pasada es claro? ¿Los avisos (`!`) explican bien el problema? ¿El timeline se entiende?

#### 3.1.1 Extruir hasta una cara curva

Extruir › «Hasta una cara» admite caras planas, cilíndricas, esféricas y toroidales. En una cara curva la extrusión llega
hasta el primer cruce que cae en el trozo **real** de la cara (los cruces con la superficie fuera de ese trozo se
atraviesan) y termina sobre la superficie completa. En una cara plana sigue usando todo el plano. No admite conos ni
superficies libres.

| ID | Haz esto | Debe pasar |
|---|---|---|
| EXT.1 | Cilindro de 20 mm de diámetro. Sketch en un plano lateral (por ejemplo XZ desplazado 30 mm), un cuadrado de 4 mm → Extruir → Extensión «Hasta una cara» → «Elegir cara de destino» y clic en la pared curva | La vista previa llega justo hasta la pared del cilindro y se queda antes de ella (no entra) |
| EXT.2 | Lo mismo con el sketch en el plano que pasa por el eje del cilindro (el perfil empieza dentro) | El sólido crece desde dentro hasta la pared |
| EXT.3 | Repite con una esfera y con un toro (Revolución de un semicírculo y de un círculo fuera del eje) | Llega a la superficie; en el toro, a su cara exterior |
| EXT.4 | Elige un cono como destino | Mensaje claro («admite caras planas, cilíndricas, esféricas y toroidales…»); la operación no se aplica |
| EXT.5 | Extruye en sentido contrario a la pared, o paralelo al eje del cilindro | Mensaje «La extrusión no llega a la cara de destino…», sin un sólido con un extremo cortado a la mitad |
| EXT.6 | Acepta, guarda, cierra y abre el `.nuf`; mueve el cilindro y mira la extrusión | Sigue llegando a la pared, y se recalcula al editar el cilindro |
| EXT.7 | Medio cilindro (Revolución de un rectángulo, 180°). Un cuadrado que sale del eje, desde cada lado, hasta su pared curva | Desde el lado de la pared llega a ella; desde el otro lado cruza el cilindro por dentro y termina en la pared de enfrente (no se detiene en el aire donde no hay pared) |
| EXT.8 | Un cuarto o un octavo de cilindro y un cuadrado que cruza la superficie solo por donde no hay pared | Mensaje «cruza la superficie de la cara de destino, pero no en el trozo que forma la cara» |

### 3.2 Operaciones de acabado (modificar)

Código: **ACA**.

Necesitas un cuerpo (la caja de 40×25×10 de «Antes de empezar»). Los botones están en el grupo **MODIFICAR** de la cinta y quedan desactivados si no hay cuerpos.

| ID | Operación | Haz esto | Debe pasar |
|---|---|---|---|
| ACA.1 | **Redondeo** (F) | Clic en una arista (se marca azul); más aristas con clics sucesivos; escribe el radio `3` | Vista previa en vivo; las aristas elegidas se redondean; Aceptar crea «Redondeo1» |
| ACA.2 | Redondeo con radio imposible | Radio `500` | Aviso en rojo en el panel, sin romper nada; Aceptar desactivado |
| ACA.3 | **Chaflán** | Igual con la distancia `2` | Cortes planos en las aristas |
| ACA.4 | Editar el redondeo | Doble clic en su ficha del timeline; cambiar el radio | El modelo se recalcula; las operaciones posteriores se mantienen |
| ACA.5 | **Vaciado** | Elegir la cara superior; espesor `2` | Cuerpo hueco abierto por arriba; probar «Hacia fuera» |
| ACA.6 | **Agujero** | Crear sketch en la cara superior, dibujar 2 círculos, Terminar; Sólido › Agujero (H); elegir el sketch; diámetro `5`, «Atravesar todo» | Dos agujeros pasantes; probar Abocardado y Avellanado (aparecen sus campos) |
| ACA.7 | **Espejo** | Sólido › Espejo; plano YZ; «Unir» | Cuerpo simétrico de 80 mm de ancho; con «Cuerpo nuevo» queda una copia aparte |
| ACA.8 | Espejo sobre una cara | Elegir «Cara plana elegida» y clic en una cara lateral | Se refleja sobre esa cara |
| ACA.9 | **Patrón lineal** | Sólido › Patrón; Lineal, eje X, cantidad `3`, distancia `60` | Tres copias separadas; con distancia `20` se funden en una pieza |
| ACA.10 | **Patrón circular** | Circular, eje Z, cantidad `4`, 360° | Cuatro copias alrededor del eje Z |
| ACA.11 | Editar la pieza original después | Cambiar la altura de la extrusión | Redondeos, agujeros, espejo y patrón siguen a la pieza |
| ACA.12 | **Cantidad ligada** | Crea el parámetro `n = 4` (Sólido › Parámetros); en el Patrón escribe `n` en «Cantidad» y Enter | El cuadro acepta la expresión y calcula 4 copias; el tooltip muestra el valor |
| ACA.13 | Cambiar el parámetro | Cambia `n` a `6` en Parámetros | El patrón se rehace con 6 copias; Ctrl+Z vuelve a 4 |
| ACA.14 | Resultado no entero | Escribe `n/2` o `n*0.4` en «Cantidad» | La cantidad se redondea al entero más cercano (mínimo 2) y no hay error |

**Fíjate en:** ¿las aristas elegidas se ven claramente? ¿El panel explica qué falta elegir? ¿Algún resultado queda «raro» (caras de más, huecos)? Lo pendiente de esta fase: ver «Pendiente de 5a–5e» en la hoja de ruta.

### 3.3 Flecha de la medida que se edita (todas las herramientas con medidas)

Código: **FLE**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| FLE.1 | Sólido › **Caja**; haz clic en «Ancho» (o entra con el teclado) | En la vista sale una flecha turquesa **de doble punta** a lo largo del ancho de la caja; al escribir otro valor la flecha se alarga con la vista previa |
| FLE.2 | Pasa a «Fondo», «Alto» y «Posición Z» | La flecha cambia a cada medida (fondo, alto, y la separación del plano en Z) |
| FLE.3 | Haz clic fuera del cuadro (por ejemplo en el vacío del panel o en la vista) | La flecha desaparece |
| FLE.4 | **Extruir** un perfil: haz clic en «Distancia», «Distancia 2» y «Desfase inicial» | Una flecha de doble punta por cada una, desde el perfil a lo largo de la normal (centrada si la dirección es Simétrica); sigue existiendo la flecha naranja para arrastrar |
| FLE.5 | **Cilindro** (diámetro y alto), **Plano** (distancia), **Mover cuerpo** (X/Y/Z), **Patrón** (distancia entre copias) y **Rosca** (diámetro y largo) | La flecha marca la medida que editas; en Mover y Patrón sale desde el centro del cuerpo |
| FLE.6 | Cambia de plano o elige una cara en Caja/Cilindro/Rosca | La flecha sigue al plano (gira con él) |
| FLE.7 | **Herramientas › Caja con tapa** (o Lightbox, Letrero): haz clic en Ancho, Fondo, Alto, Pared, Base, Grosor de la tapa… | La flecha turquesa marca esa medida sobre la pieza (el grosor de la pared en el borde derecho, la base en el fondo, etc.); en el Letrero solo el grosor de la placa |
| FLE.8 | **Ángulos**: Plano (ángulo, con un valor distinto de 0), Mover cuerpo (ángulo) y Patrón circular (ángulo total) | En vez de una flecha recta sale un **arco de doble punta** alrededor del eje de giro, con la apertura del ángulo |
| FLE.9 | **Revolución** con un ángulo (no vuelta completa): haz clic en «Ángulo» (y en «Ángulo 2» con «Dos lados») | Un arco de doble punta alrededor del eje, pasando por el perfil; con «Simétrica» el arco queda repartido a ambos lados del perfil |
| FLE.10 | **Extruir** con «Ángulo» (conicidad) distinto de 0, en el lado 1 y en el 2 | Un arco en la base de la extrusión entre la dirección recta y la pared inclinada |
| FLE.11 | **Unión**: elige dos caras y haz clic en «Distancia», «Ángulo» (Giratoria) y «Desplazamiento» (Deslizante) | Flechas sobre la cara fija: la distancia a lo largo de su normal, el desplazamiento a lo largo de su eje X y un arco alrededor de su normal |
| FLE.12 | **Engranaje**: haz clic en «Giro inicial», «Ángulo de presión» y «Ángulo de hélice» (con hélice distinta de 0) | Un arco alrededor del eje (giro inicial, desde el eje X), otro en el punto primitivo (presión) y otro entre el eje y el diente (hélice) |
| FLE.13 | **Caja**: radio de esquinas con un valor distinto de 0; **Rosca** con «Cortar»: «Holgura» | Una flecha del tamaño del radio desde la esquina; en la rosca, una flecha con el diámetro del agujero resultante (diámetro + 2 × holgura) |
| FLE.14 | **Generadores** (Caja con tapa, Lightbox, Letrero, Par de engranajes): haz clic en radio de esquinas, grosor del labio, diámetro de postes y agujeros (cierre con tornillos), margen, anilla y agujero de la anilla (llavero), módulo, dientes y agujeros del par | Cada medida tiene su flecha sobre la pieza: los postes y los agujeros en la esquina de arriba a la derecha, los agujeros del par en el centro de la rueda, el módulo y los dientes como Ø primitivo |
| FLE.15 | **Redondeo / Chaflán** (tamaño), **Vaciado** (espesor), **Agujero** (diámetro, profundidad, cabeza), **Engranaje** (módulo y dientes = diámetro primitivo, ancho, agujero), **Rosca** (paso) y **Posición X / Y** de Caja, Cilindro, Rosca y Engranaje | Cada medida tiene su flecha: el tamaño del redondeo desde la arista hacia dentro (solo aristas rectas), el espesor desde la cara abierta, el agujero en el primero de los círculos del sketch, X e Y desde el origen del plano |
| FLE.16 | Acepta o cancela la herramienta | No queda ninguna flecha en la vista |

### 3.4 Mover y girar arrastrando (estilo Orca)

Código: **MOV**. Estado: en prueba.

| ID | Haz esto | Debe pasar |
|---|---|---|
| MOV.1 | Crea una caja, Sólido › **Mover cuerpo** | Sobre la caja salen tres flechas (X rojo, Y verde, Z azul) en su centro; el panel tiene botones **Mover** y **Girar** |
| MOV.2 | Arrastra la flecha roja con el ratón | Una **sombra azul translúcida** de la caja se desplaza por X; la caja de verdad se queda; el cuadro «Mover en X» va cambiando; la app no se pausa |
| MOV.3 | Suelta el ratón | La sombra desaparece, la caja se coloca ahí (se recalcula) y las flechas se van con ella |
| MOV.4 | Pulsa **Girar** | Aparecen tres anillos con marcas cada 45° (más largas en 0°, 90°, 180°, 270°) |
| MOV.5 | Arrastra un anillo despacio (puedes agarrarlo en cualquier punto) | Cada anillo tiene una **perilla** (el 0°). El ángulo que se aplica es el del cursor sobre el anillo medido desde esa perilla; la sombra gira y se dibuja un **arco** desde la perilla hasta el cursor. Cuando el cursor pasa sobre una marca (a menos de 3°) la pieza queda **exactamente** en ese ángulo y el panel dice «(marca)»; entre marcas gira libre |
| MOV.6 | Arrastra un anillo y pasa el cursor despacio sobre una marca (por ejemplo la de 90°) | Al acercarte (a menos de 4°) el giro se **pega** a la marca y se dibuja una **recta amarilla** por el centro del anillo, en el plano del anillo y en la dirección de esa marca: «estás alineado con 90°». Sigue pegado aunque muevas el cursor un poco (hasta unos 9° de distancia); al alejarte más se suelta, la recta desaparece y el giro vuelve a ser libre |
| MOV.7 | Agarra un anillo (o una flecha) y arrastra | Los **otros dos** desaparecen mientras arrastras, para ver el giro (o el movimiento) sin estorbos; al soltar vuelven los tres |
| MOV.8 | Mientras arrastras una flecha o un anillo | La **sombra azul translúcida** se ve y sigue al ratón (la pieza real se queda donde estaba hasta que sueltas) |
| MOV.9 | Con cualquier herramienta abierta (Caja, Mover…), gira la vista y arrastra un anillo rápido | La vista va fluida (antes, con cualquier panel abierto, iba a unas 9 imágenes por segundo aunque no tocaras nada); la barra de estado dice el ángulo mientras arrastras; el ventilador/CPU no se dispara con la herramienta abierta y quieta |
| MOV.10 | Agarra un anillo lejos de la perilla, por ejemplo en 90° | La pieza salta a ese ángulo al empezar a mover (como Orca Slicer); dime si el salto te molesta |
| MOV.11 | Da una vuelta completa o más con el anillo | No se rompe: el valor se mantiene entre −180° y 180° |
| MOV.12 | Gira con el anillo azul (Z) 45° y luego con el rojo (X) 30° y con el verde (Y), sin aceptar | Cada anillo gira la pieza sobre **su propio eje del mundo** (el que dibuja el anillo), aunque ya esté girada por otro; la pieza no «se tuerce» sobre ejes que no son los del anillo. Los cuadros «Girar en X/Y/Z» muestran los tres giros equivalentes (pueden cambiar los tres a la vez) |
| MOV.13 | Teclea valores en los cuadros (Mover en X…, Girar en Z…) | La vista previa se actualiza como siempre; las flechas o los anillos siguen al cuerpo |
| MOV.14 | Con otro cuerpo en la escena, mueve el primero | El otro cuerpo se queda a la vista como referencia |
| MOV.15 | Gira la vista con el ratón fuera de las flechas y los anillos | La vista orbita normal (no se agarra el gizmo) |
| MOV.16 | Haz zoom con la rueda con los anillos (o las flechas) puestos, rápido y despacio | La vista va fluida; las flechas o los anillos **desaparecen mientras haces zoom** y reaparecen (con su tamaño de siempre) cerca de 1 s después de parar el último zoom; si haces zoom a ráfagas, no reaparecen entre una y otra |
| MOV.17 | Acepta, guarda, cierra, abre y haz doble clic en el Mover del historial | Vuelven los mismos valores de movimiento y giro; deshacer deja el cuerpo como estaba |
| MOV.18 | Abre un diseño de antes con un Mover que giraba (ángulo) sobre Z | Se ve ese giro en «Girar en Z» y el cuerpo queda igual |
| MOV.19 | Un cuerpo con rosca: mover o girar arrastrando | La sombra es una caja (no se dibuja la rosca mientras arrastras) y no se cuelga |
| MOV.20 | Haz un **Llavero** (Herramientas › Letrero o llavero), selecciona la placa y el texto con **Shift + clic** en una cara de cada uno y abre **Mover cuerpo** | La lista «Cuerpos» tiene marcados los dos y dice «Se mueven 2 cuerpos juntos»; las flechas salen en el centro de los dos |
| MOV.21 | Arrastra una flecha y suelta | Placa y texto se mueven **juntos** (la sombra los muestra a los dos) |
| MOV.22 | Gira con un anillo | Los dos giran como un bloque, por el centro de los dos |
| MOV.23 | Marca y desmarca cuerpos en la lista de «Cuerpos» | La vista previa y el gizmo se ajustan a los marcados; con más de uno la casilla de «Crear una copia» queda desactivada |
| MOV.24 | Guarda, cierra, abre y edita el Mover del historial | Vuelven marcados los mismos cuerpos |

### 3.5 Geometría de referencia y parámetros

Código: **REF**.

#### 3.5.1 Planos y ejes de construcción

| ID | Haz esto | Debe pasar |
|---|---|---|
| REF.1 | Sólido › **Plano** (grupo CONSTRUIR); base «Plano XY», distancia `25` | Cuadrado turquesa a 25 mm; Aceptar → «Plano1» en el timeline y en el navegador (carpeta «Planos de construcción») |
| REF.2 | Crear sketch → clic en el plano turquesa | «Sketch en Plano1»: dibuja y extruye; el cuerpo nace a 25 mm de altura |
| REF.3 | Editar Plano1 (doble clic): distancia `40` | Todo lo que se apoya en él sube |
| REF.4 | Plano con ángulo: ángulo `45`, girar alrededor del «Eje X del plano» | Plano inclinado |
| REF.5 | Casilla del plano en el navegador | Lo oculta y lo muestra |
| REF.6 | **Espejo** con un plano de construcción | El plano aparece en la lista «Plano de simetría» |
| REF.7 | Sólido › **Eje**; primer plano XY, segundo XZ | Línea turquesa a lo largo del eje X; «Ninguno» da la perpendicular al primero |
| REF.8 | Revolución y Patrón circular con ese eje | El eje aparece en las listas de «Eje» |

#### 3.5.2 Parámetros y expresiones

| ID | Haz esto | Debe pasar |
|---|---|---|
| REF.9 | Sólido › **Parámetros**; Añadir; nombre `alto`, expresión `3*4` | Valor `12` al lado; nombre repetido o inválido → aviso rojo y Aceptar desactivado |
| REF.10 | En **Extruir** (o Plano, Redondeo…) escribir en «Distancia» `alto*2` y Enter | Se calcula `24`; el tooltip del cuadro muestra el valor |
| REF.11 | Volver a Parámetros y cambiar `alto` a `20` | La pieza se recalcula (distancia 40) |
| REF.12 | Escribir `sin(30)*10`, `2 in`, `1,5 + alto` | `5`, `50.8`, `21.5` |
| REF.13 | Referencia circular: `a = b+1`, `b = a+1` | Aviso «Referencia circular» |
| REF.14 | Borrar un parámetro que se usa | La operación pasa a error con un mensaje claro |
| REF.15 | Guardar y reabrir | Parámetros y expresiones siguen; cambiar un parámetro sigue recalculando |
| REF.16 | Ctrl+Z tras cambiar parámetros | Vuelve al valor anterior |
| REF.17 | Con `alto` usado en una extrusión y en otro parámetro (`doble = alto*2`): Parámetros → renombrar `alto` a `espesor` → Aceptar | La fila `doble` pasa sola a `espesor*2` mientras escribes; la pieza no cambia ni queda en error; al reabrir la operación, su campo dice `espesor` |
| REF.18 | Ctrl+Z tras renombrar | Vuelven el nombre `alto` y la expresión de la operación; la pieza sigue igual |
| REF.19 | Renombrar a un nombre que ya existe | Aviso «repetido» y Aceptar desactivado; al corregirlo, las expresiones se actualizan con el nombre final |
| REF.20 | Parámetro `radio = 5`. Sketch con un círculo acotado (Ø o R) → doble clic en la cota → escribir `radio` (o `radio*2` en un Ø) → Aceptar → Terminar sketch | La medida es la del parámetro; el cuadro de la cota mostraba la expresión al reabrirla |
| REF.21 | Sólido › Parámetros → cambiar `radio` a `8` | El sketch y la pieza se recalculan (el círculo crece); Ctrl+Z lo devuelve |
| REF.22 | Poner `radio = -2` (o un valor que haga imposible el sketch) | La operación del sketch queda en «!» con el motivo («debe ser mayor que cero» o «no tiene solución»); al corregir, vuelve a la normalidad |
| REF.23 | Cota angular con `ang = 30` y expresión `ang` (o `ang + 15`) | Se entiende en grados; guardar y reabrir conserva la expresión |
| REF.24 | Renombrar `radio` a `r` en Parámetros | La cota sigue ligada (reabrirla muestra `r`) y la pieza no cambia |

**Fíjate en:** ¿se entiende que un cuadro «tiene una expresión»? ¿Escribir un nombre de parámetro es natural? ¿Falta algo (las cotas del sketch aún no se ligan; ver «Pendiente de 6c»)?

### 3.6 Generadores de piezas (engranaje, rosca, barrido y loft)

Código: **GEN**.

#### 3.6.1 Engranaje

| ID | Haz esto | Debe pasar |
|---|---|---|
| GEN.1 | Sólido › **Engranaje** (grupo GENERAR); valores por defecto | Engranaje de 20 dientes, módulo 2, Ø primitivo 40 mm, con agujero de 5 mm |
| GEN.2 | Cambiar dientes a `12`, `40`, `100` | Se rehace cada vez; con muy pocos dientes (< 6) o ángulos raros, mensaje claro |
| GEN.3 | Ángulo de hélice `20` | Dientes inclinados (helicoidal); es más lento |
| GEN.4 | Aceptar y mirar de cerca | Flancos lisos, sin dientes «rotos»; probar el volumen con Archivo › Exportar STL en un slicer |
| GEN.5 | Dos engranajes del mismo módulo: crear uno, mover el otro con un plano de construcción desplazado | Los dientes deberían poder encajar (distancia entre ejes = módulo × (z1+z2)/2) |

#### 3.6.2 Rosca

| ID | Haz esto | Debe pasar |
|---|---|---|
| GEN.6 | Sólido › **Rosca**; medida M8 × 1.25, largo `10` | Varilla roscada; el panel dice el diámetro menor (6.47 mm) |
| GEN.7 | Cambiar «Medida» a M6; luego tocar el diámetro a mano | La medida pasa a «Personalizada» |
| GEN.8 | «Rosca a izquierdas» | La hélice gira al otro lado |
| GEN.9 | **Rosca interior**: caja, agujero de Ø 6.47 con la herramienta Agujero, y rosca M8 con operación «Cortar» | El agujero queda roscado |

#### 3.6.3 Barrido y loft

| ID | Haz esto | Debe pasar |
|---|---|---|
| GEN.10 | Sólido › **Barrido**: sketch A (perfil, un cuadrado en el plano XZ centrado en el origen), sketch B (trayectoria: una línea y otra en L en el plano XY desde el origen); elegir el cuadrado y las líneas | Barra cuadrada que sigue la L; prueba también «Fija» |
| GEN.11 | Barrido con una trayectoria rota (dos líneas sin tocarse) | Mensaje de error claro |
| GEN.12 | Sólido › **Loft**: cuadrado de 10 en XY y otro de 4 en un plano de construcción a 20 mm; elegirlos **en ese orden** | Tronco de pirámide; probar «Suave» y «Recto» y tres perfiles |

**Fíjate en:** ¿el cálculo de engranajes/roscas tarda demasiado (anota cuánto)? ¿La elección del perfil y la trayectoria en el barrido/loft es clara o confusa? ¿Los textos del panel guían? Esto es lo menos probado en la interfaz (ver «Pendiente de 7c/7d»).

#### 3.6.4 Colocar rosca y engranaje sobre una cara

| ID | Haz esto | Debe pasar |
|---|---|---|
| GEN.13 | Con una pieza hecha, Sólido › Rosca y haz clic en una cara plana de la pieza | «Plano o cara» pasa a «Cara elegida» y X/Y se ponen en el centro de la cara; la rosca aparece sobre la cara |
| GEN.14 | Cambia Posición X o Y | La rosca se mueve en el plano de esa cara |
| GEN.15 | Cambia la operación a «Cortar (rosca interior)» | Aparece «Holgura»; el largo pasa a negativo y la rosca se corta hacia dentro de la pieza |
| GEN.16 | Sube la holgura (0,3 – 0,5) | El agujero sale más ancho (el texto dice el Ø del agujero) |
| GEN.17 | Haz un tornillo M8 con «Cuerpo nuevo» y pruébalo (impreso) en esa tuerca | Entra sin forzar con holgura ≈ 0,15–0,2 mm |
| GEN.18 | Haz lo mismo con Sólido › Engranaje (clic en una cara, X/Y, «Cortar») | Se coloca en la cara; con «Cortar» deja un hueco con la forma del engranaje |
| GEN.19 | Guarda, cierra y abre el diseño | Posición, cara y holgura se conservan |

### 3.7 Multi-cuerpo y ensambles

Código: **MUL**.

#### 3.7.1 Mover y combinar cuerpos

| ID | Haz esto | Debe pasar |
|---|---|---|
| MUL.1 | Con un cuerpo, Sólido › **Mover cuerpo** (grupo MODIFICAR); Mover en X `60` | Vista previa del cuerpo desplazado; Aceptar → ficha «Mover1» |
| MUL.2 | Ángulo `45` con el eje Z «Centro del cuerpo» y luego «Origen» | Gira sobre sí mismo o alrededor del origen |
| MUL.3 | Marcar «Crear una copia» | El original se queda y aparece «Cuerpo2» |
| MUL.4 | Con dos cuerpos, clic en una cara del segundo mientras Mover está abierto | El desplegable «Cuerpo» cambia a ese cuerpo |
| MUL.5 | Sólido › **Combinar cuerpos**; destino Cuerpo1, marcar Cuerpo2, Unir (que se toquen o solapen) | Un solo cuerpo; con «Cortar» queda el destino con el hueco; «Intersecar» deja solo lo común |
| MUL.6 | Combinar con «Conservar los cuerpos herramienta» | Los dos cuerpos siguen en el navegador |
| MUL.7 | Editar la pieza original (cambiar la altura de su extrusión) | Mover y Combinar se rehacen sobre ella |

#### 3.7.2 Componentes (organizar)

| ID | Haz esto | Debe pasar |
|---|---|---|
| MUL.8 | Con dos cuerpos, Sólido › **Componentes** (grupo ORGANIZAR): ponle nombre a cada uno (`Base`, `Tapa`) y escribe `Carcasa` como componente de los dos | En el navegador aparece la carpeta «Carcasa» con los dos cuerpos con sus nombres nuevos |
| MUL.9 | Casilla de «Carcasa» en el navegador | Oculta y muestra los dos cuerpos a la vez |
| MUL.10 | Guardar, cerrar y abrir; luego Ctrl+Z tras cambiar nombres | Nombres y componentes se conservan; deshacer los revierte |
| MUL.11 | Borrar el texto de un nombre | El cuerpo recupera su nombre de fábrica (Cuerpo1) |

#### 3.7.3 Unión entre cuerpos

| ID | Haz esto | Debe pasar |
|---|---|---|
| MUL.12 | Dos cajas separadas (una con **Mover** → «Crear una copia»). Sólido › **Unión** (grupo ORGANIZAR). Clic en la cara **superior** de la primera y luego en la cara **inferior** de la segunda (hay que girar la vista para ver el fondo) | El panel dice «Fija: … — Móvil: …» y la segunda caja se apoya sobre la primera, centrada |
| MUL.13 | Distancia `5` | Quedan separadas 5 mm |
| MUL.14 | Tipo «Giratoria» con ángulo `90` | La segunda gira sobre la normal de la cara |
| MUL.15 | Tipo «Deslizante» con desplazamiento `10` | Se corre 10 mm sobre la cara |
| MUL.16 | Botón «Intercambiar fija y móvil» | Ahora se mueve la otra caja |
| MUL.17 | Clic en una cara ya elegida | La quita de la elección |
| MUL.18 | Elegir dos caras del mismo cuerpo | Error claro en el panel |
| MUL.19 | Cambiar la altura de la caja fija y volver a la Unión | La caja móvil sube con ella |

**Fíjate en:** ¿se entiende qué cuerpo se mueve y cuál es la pieza fija y cuál la móvil en una unión? ¿Las caras elegidas se distinguen en la vista (hoy no se resaltan)? ¿Mover con números es suficiente o echas de menos arrastrar con el ratón? (ver «Pendiente de 8a–8c»).

### 3.8 Inspeccionar: medir y sección

Código: **INS**.

#### 3.8.1 Medir

| ID | Haz esto | Debe pasar |
|---|---|---|
| INS.1 | Con un cuerpo, SÓLIDO › INSPECCIONAR › **Medir** | Aparece un panel arriba a la izquierda de la vista: «Elige una cara o una arista» |
| INS.2 | Clic en una cara plana | Área y normal |
| INS.3 | Clic en una arista recta; luego en el borde de un agujero | Longitud; radio, diámetro y centro |
| INS.4 | Clic en dos caras paralelas | Distancia mínima y «Paralelas, separadas X mm» |
| INS.5 | Clic en dos agujeros del mismo tipo | «Distancia entre ejes/centros» |
| INS.6 | Esc, o iniciar un sketch o una herramienta | El panel desaparece y Medir se apaga |

#### 3.8.2 Sección

| ID | Haz esto | Debe pasar |
|---|---|---|
| INS.7 | Con un cuerpo, Sólido › **Sección** | Aparece un panel con el plano, la posición y «Ver el otro lado»; la pieza se corta por la mitad y el corte se ve tapado en azul |
| INS.8 | Cambiar la posición | El corte se mueve a lo largo del eje |
| INS.9 | Cambiar de plano XY / XZ / YZ | El corte vuelve al centro de la pieza en ese eje |
| INS.10 | «Ver el otro lado» | Se ve la mitad contraria |
| INS.11 | Esc, o iniciar un sketch | La pieza vuelve entera; el modelo no cambió |

---

## 4. Herramientas (generadores)

### 4.1 Lightbox

Código: **LIG**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| LIG.1 | Pestaña HERRAMIENTAS (o menú Herramientas) › **Lightbox** | Se abre el formulario a la derecha y se ve una caja abierta por delante con su tapa (placa y labio, un solo sólido) al lado |
| LIG.2 | Cambia Ancho, Alto, Pared… | La vista previa se actualiza (con «Calculando…» un momento) |
| LIG.3 | Cambia «Forma» a Redonda | Desaparecen Ancho y Alto, aparece Diámetro, y la caja es redonda |
| LIG.4 | Pon Ancho 20 y Pared 20 (un hueco imposible) | Avisa en rojo de la operación que falla y no deja aceptar, sin cerrar la herramienta |
| LIG.5 | Aceptar | Navegador: componente «Lightbox» con solo dos cuerpos, «Caja» y «Tapa»; el historial muestra las 6 operaciones «Lightbox: …» |
| LIG.6 | Sólido › Parámetros | Aparecen ancho, alto, profundidad, pared, frente, tapa, labio, holgura y cable; al cambiar uno, todo se recalcula |
| LIG.7 | Ctrl+Z | Desaparece todo de una vez (operaciones, parámetros y componente) |
| LIG.8 | Genera un segundo Lightbox | Sus parámetros salen como ancho_2, alto_2… y no pisan los del primero |
| LIG.9 | Exporta o imprime los dos cuerpos | La revisión no avisa de paredes finas con los valores por defecto |
| LIG.10 | Sólido › Caja / Cilindro | Primitivas con posición, centradas o no, con alto ± y operación; doble clic en una operación «Lightbox: …» del historial abre su panel |
| LIG.11 | Compara con tus lightboxes de referencia (Illescas, Aguilar) | Las medidas son las de `docs/generadores.md`: 175 × 131 × 37 con pared 2; para igualar la de Illescas pon ancho 174,664, alto 132,036, profundidad 37 y holgura 0 |
| LIG.12 | Aceptar y selecciona la tapa en el Navegador o con un clic en ella | Se marca **toda** la tapa (placa y labio) como un solo cuerpo; no hay marco, labio ni cara sueltos |
| LIG.13 | Forma «Redonda», Aceptar | También salen solo «Caja» y «Tapa»; Ctrl+Z lo deshace todo de una vez |
| LIG.14 | Cambia «Frente», «Alto del labio» y «Grosor del labio» con el formulario abierto | La flecha turquesa mide la placa de la tapa, el labio desde su borde de arriba y el grosor de su pared |

### 4.2 Caja con tapa

Código: **CAJ**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| CAJ.1 | Herramientas › **Caja con tapa** | Se ve una caja abierta por arriba y su tapa al lado; por defecto con labio |
| CAJ.2 | Cambia «Cierre» a «Tornillos en las esquinas» | Aparecen cuatro postes con agujero en la caja y cuatro agujeros en la tapa; desaparecen los campos del labio y salen los de los postes |
| CAJ.3 | Aceptar y abrir Sólido › Parámetros | Ancho, fondo, alto, pared… como parámetros; al cambiar «alto» la caja y los postes crecen |
| CAJ.4 | Imprime la tapa con labio sobre la caja | Entra a presión sin forzar con holgura 0,2 (subir a 0,3 si queda dura) |
| CAJ.5 | Prueba un tornillo M3 en un poste y en la tapa | Se enrosca en el poste (Ø2,5) y pasa libre por la tapa (Ø3,4) |
| CAJ.6 | Aceptar con cada cierre y mira el Navegador | Solo hay dos cuerpos, «Caja» y «Tapa»; la tapa (placa, labio y agujeros) es uno solo. No cambia nada respecto a antes (la Caja con tapa ya era así) |

### 4.3 Letrero o llavero

Código: **LET**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| LET.1 | Herramientas › **Letrero o llavero** | Se ve una placa con «Nufio» en relieve |
| LET.2 | Escribe otro texto, cambia la fuente y el tamaño | Las letras y la placa se actualizan (la placa se ajusta al texto) |
| LET.3 | Cambia «Tipo» a Llavero | Aparece la anilla con su agujero a la izquierda y los campos de la anilla |
| LET.4 | Cambia «Texto» a Grabado | Las letras quedan hundidas en la placa (un solo cuerpo) |
| LET.5 | Prueba letras con hueco (O, A, B, 8) | El hueco queda libre, no se rellena |
| LET.6 | Aceptar | Navegador: «Placa» y «Texto» en el componente; el historial tiene el sketch «…: letras» y sus operaciones |
| LET.7 | Cambia «Texto» a «En relieve (otro cuerpo)», acepta e imprime con el texto en otro color | El cuerpo «Texto» se asigna a otro filamento sin tocar la placa |
| LET.8 | Por defecto («En relieve (un solo cuerpo)») acepta | Navegador: un solo cuerpo «Placa» en el componente; el texto está unido a la placa, sin juntas visibles |
| LET.9 | Igual con «Tipo» Llavero | Placa, texto y anilla (con su agujero) son un solo cuerpo; mover o girar lo lleva todo junto |
| LET.10 | Reabre el letrero (doble clic en una operación) y cambia el grosor, el relieve o el modo | Se rehace en su sitio; Ctrl+Z lo deshace de una vez; un diseño guardado antes sigue abriendo con su modo |

### 4.4 Par de engranajes

Código: **PAR**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| PAR.1 | Herramientas › **Par de engranajes** | Se ven dos ruedas (12 y 24 dientes) con los dientes encajados, sin tocarse; el panel dice la relación 12:24 y la distancia 36,15 mm |
| PAR.2 | Cambia los dientes (por ejemplo 15 y 30) y el módulo | Las ruedas se recolocan solas a la nueva distancia y siguen encajadas |
| PAR.3 | Pon un ángulo de hélice de 15° | Las dos ruedas llevan la hélice en sentido contrario |
| PAR.4 | Aceptar y abrir Sólido › Parámetros | `modulo`, `dientes_1`, `dientes_2`… y `giro_rueda_2` (calculado); al cambiar un parámetro, todo se recalcula |
| PAR.5 | Imprime las dos ruedas con sus ejes | Giran juntas sin atascarse con el juego de 0,15 mm (subirlo a 0,25 si rozan) |
| PAR.6 | Haz doble clic en «Engranaje: rueda 2» del historial | Se abre su panel con Posición X, Giro inicial y los demás campos ligados |
| PAR.7 | Herramientas › **Engranaje interno** | Se ve una corona (aro con los dientes hacia dentro, 36 dientes) y un piñón de 12 dientes dentro, tocando por el lado derecho, con los dientes encajados y sin solaparse; el panel dice 12:36 y distancia 23,85 mm |
| PAR.8 | Pon 30 dientes al piñón | El panel avisa «con menos de 8 dientes de diferencia los dientes se estorban» |
| PAR.9 | Con el piñón de 12: corta una sección (Inspeccionar) o mira de cerca el engrane | Los dientes del piñón entran en los huecos de la corona sin atravesarla |
| PAR.10 | Aceptar, abrir Sólido › Parámetros y cambiar `dientes_corona` a 48 | La corona crece y el piñón se recoloca a la nueva distancia; Ctrl+Z deshace todo de una vez |
| PAR.11 | Doble clic en «Engranaje: corona» del historial | Se abre el panel del Engranaje con Tipo «Interno (corona)» y el campo «Aro»; al cambiar el aro, el grosor exterior cambia |
| PAR.12 | Imprime la corona y el piñón | El piñón gira dentro de la corona sin atascarse con el juego de 0,15 mm (0,25 si roza) |
| PAR.13 | Herramientas › **Cremallera** | Se ve una barra dentada (10 dientes) debajo de una rueda de 20 dientes; un diente de la rueda cae en un hueco de la barra, sin tocarse; el panel dice cuánto avanza la barra por vuelta (125,66 mm) |
| PAR.14 | Cambia los dientes de la rueda a 21 y el módulo a 3 | La barra se recoloca y sigue engranando (un diente de la rueda sobre un hueco de la barra) |
| PAR.15 | Pon el ángulo de presión en 30° y luego intenta 35° | Con 30° sale la barra con dientes finos; el campo no pasa de 30° |
| PAR.16 | Aceptar, guardar y reabrir el archivo | La cremallera y la rueda vuelven igual; doble clic en «Engranaje: cremallera» muestra Tipo «Cremallera» y el grosor de la base |
| PAR.17 | Abre un diseño guardado antes de este cambio (con un par de engranajes) | Las ruedas siguen siendo exteriores y se ven igual |
| PAR.18 | Sólido › **Engranaje** (20 dientes, módulo 2): pon «Chaflán de la base» en 0,2 | La vista previa se rehace sin error y, de cerca, el borde de la cara de abajo (la que queda sobre la cama) tiene un chaflán pequeño en el contorno de los dientes y **no** en el agujero central. Con una medida grande (0,5 o más) o en algunas medidas sueltas sale un aviso en español («prueba con otra cercana»); con hélice distinta de 0 también avisa |
| PAR.19 | Pon «Juego del diente» en 0,2 y compara con 0 (Inspeccionar › Medir el grueso de un diente, o a ojo) | Los dientes quedan más delgados; el diámetro exterior no cambia. Con la corona y la cremallera pasa igual. Imprime dos ruedas con juego 0,2 en cada una: engranan sin atascarse sin separar los ejes |
| PAR.20 | Aceptar, guardar y reabrir; luego un diseño anterior | Los dos campos vuelven con su valor; un diseño viejo abre con juego 0 y chaflán 0 |
| PAR.21 | Herramientas › **Par de engranajes**: pon «Adelgazar el diente» en 0,2; luego prueba «Engranaje interno» y «Cremallera» | Los dientes de las ruedas (y de la barra o la corona) quedan más delgados y los ejes no se mueven; el parámetro `adelgazar` aparece en Sólido › Parámetros |
| PAR.22 | En los mismos tres generadores pon «Chaflán de la base» en 0,2 (con hélice 0) | Se achaflana la cara de abajo de cada pieza. Si sale el aviso «prueba con otra cercana», prueba 0,18 o 0,25 y anota los dientes y la medida |

### 4.5 Todos los generadores: reabrir, recordar y restablecer

Código: **GRE**.

| ID | Haz esto | Debe pasar |
|---|---|---|
| GRE.1 | Con el Lightbox ya aceptado, clic derecho en cualquiera de sus operaciones del historial › **Reabrir «Lightbox»…** | Se abre el formulario con las **mismas respuestas** con que se generó; la vista previa sustituye al diseño anterior (no se ve duplicado) |
| GRE.2 | Cambia solo medidas (Pared, Ancho…) y acepta | El diseño cambia **en su sitio**: las operaciones conservan su nombre y su orden, los cuerpos conservan sus nombres y el componente; los parámetros cambian de valor; Ctrl+Z lo devuelve todo |
| GRE.3 | Reabre y cambia algo que añade operaciones (por ejemplo «Agujero del cable» de 0 a 6) | Se rehace el grupo entero en el mismo sitio del historial; Ctrl+Z / Ctrl+Y funcionan |
| GRE.4 | Antes de reabrir, añade un Patrón o Redondeo sobre un cuerpo del generador y reabre cambiando algo que añade operaciones | Aviso en la barra de estado explicando que otra operación depende de las actuales; el formulario sigue abierto. Con cambios que no alteran la estructura sí se aplica |
| GRE.5 | Guarda, cierra y abre; reabre el generador | Sigue disponible con las respuestas de antes (también en el Letrero, el Par de engranajes y la Caja con tapa) |
| GRE.6 | Deja el ratón sobre un campo de un formulario de generador | Sale un tooltip con la explicación del campo (no hay texto fijo bajo el campo) |
| GRE.7 | Genera un Lightbox cambiando medidas, acepta; abre de nuevo Lightbox (o Caja con tapa, etc. con el suyo) | El formulario nuevo parte de las **últimas respuestas aceptadas** de ese generador; cada generador recuerda las suyas; al reabrir uno ya hecho (GRE.1) manda lo guardado en ese diseño |
| GRE.8 | Abre un generador (Caja con tapa, Lightbox, Letrero), cambia varios valores y acepta; abre el mismo generador otra vez | El formulario parte de lo último que aceptaste |
| GRE.9 | Pulsa **Restablecer valores** (al final del formulario) | Todas las respuestas vuelven a las de fábrica y la vista previa se actualiza |
| GRE.10 | Cancela, y abre el generador otra vez | Parte de los valores de fábrica (se olvidó lo recordado) |

---

### 4.6 Familias de herramientas y largo de la cremallera (`CRE`)

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| CRE.1 | Abre la pestaña **HERRAMIENTAS** y el menú **Herramientas** | Las herramientas salen en **tres grupos**: **CAJAS** (Lightbox, Caja con tapa), **LETREROS** (Letrero o llavero) y **ENGRANAJES** (Par de engranajes, Engranaje interno, Cremallera); el menú tiene un título por familia |
| CRE.2 | Herramientas › Cremallera: mira las preguntas | Hay «**Largo de la barra**» (Por número de dientes / Largo exacto / Largo con dientes completos); con «Por número de dientes» se ven los dientes, como antes |
| CRE.3 | Elige «**Largo exacto**»; escribe 100 y acepta | Aparece «Largo exacto de la barra» (y se ocultan los dientes); la barra mide **justo 100 mm** (compruébalo con Medir) y el último diente sale cortado; el resumen dice los dientes y que el último va cortado |
| CRE.4 | Repite con 77,7 y con 40 | Siempre mide lo pedido; la barra y la rueda siguen engranando bien (el hueco bajo la rueda) |
| CRE.5 | Elige «**Largo con dientes completos**» con 100 mm | La barra se ajusta al entero de dientes más cercano (16 dientes, 100,5 mm con módulo 2) y el último diente sale entero |
| CRE.6 | Con el diseño hecho, en Sólido › Parámetros cambia `largo` | La barra se rehace con el largo nuevo (los dientes se recalculan solos) |
| CRE.7 | Guarda, cierra, abre; clic derecho en la cremallera › Reabrir | El largo y el modo siguen puestos; un diseño de cremallera **hecho antes de este cambio** se reabre por dientes, igual que estaba |
| CRE.8 | Pon un largo mayor que el que cabe (módulo 20 y largo 2000: más de 300 dientes) | El panel avisa con un mensaje claro, sin cerrar nada |

## 5. Pruebas automáticas y transversales

### 5.1 Prueba automática de la interfaz

No hace falta tocar nada: abre la ventana real, recorre las herramientas y los generadores y escribe un informe.

```
NufioCAD.exe --prueba-interfaz [--informe ruta.txt] [--umbral-ms 20000]
```

Variables de entorno útiles al depurar la prueba: `NUF_PRUEBA_SOLO=<texto>` (solo los pasos cuyo nombre lo contiene), `NUF_PRUEBA_PILA_MS=<ms>` (desde cuántos ms de parada se anota la pila, por defecto 1000) y `NUF_PRUEBA_CAPTURAS=<carpeta>` (guarda la captura de la vista previa translúcida).

- Abre cada generador (Lightbox, Caja con tapa, Letrero o llavero, Par de engranajes), cambia un valor, acepta y deshace; hace lo mismo con Rosca, Engranaje, Caja y Cilindro; cancela una herramienta; cambia las unidades a cm y vuelve a mm; activa y desactiva Sección y Medir con un cuerpo; cambia el diámetro de un cilindro con una rosca cortante encima (debe volver al instante y calcular en segundo plano); y comprueba que al final no queda nada.
- **Pasos de medición de fluidez (Fase 13, F1):** reproducen lo que hoy se hace en el hilo de la interfaz y puede tardar: exportar STL/OBJ/STEP y contar triángulos de una pieza con rosca; abrir el diálogo de Exportar con 5 cuerpos, marcar y desmarcar y cambiar la calidad; importar un STEP con un engranaje; medir caras de una rosca; recargar la pantalla de inicio con 10 recientes; y un patrón lineal de 50 copias. No fallan por tiempo (solo por el umbral): las llamadas de más de 100 ms salen en el informe con su duración y sirven de línea base para comprobar cada rama de la Fase 13 (ver [`auditoria-3-fluidez.md`](auditorias/auditoria-3-fluidez.md)).
- Falla si algo no se abre o no se cierra, si deshacer deja restos, o si la interfaz se para más del umbral (por defecto 20 s: en Debug OpenCASCADE es lento).
- El informe (por defecto `%TEMP%\NufioCAD-prueba-interfaz.txt`) dice la parada más larga de cada paso y qué la provocó, y, si hubo paradas de más de un segundo, **dónde estaba el programa** (la pila de llamadas).
- Usa ajustes y carpetas propios (no toca tus ajustes, tus recientes ni las copias de recuperación). Devuelve 0 si todo va bien.

### 5.4 Vigilante de paradas (`NUF_VIGILANTE`)

Para encontrar, **usando la aplicación de verdad**, lo que deja la ventana congelada. Un hilo aparte mira si la interfaz sigue dando señales de vida; si se queda parada más de 200 ms anota cuánto duró y, en Windows, dónde estaba (la pila de llamadas).

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| VIG.1 | En PowerShell: `$env:NUF_VIGILANTE = 1; .\NufioCAD.exe` (en Linux: `NUF_VIGILANTE=1 ./NufioCAD`) | La aplicación abre y se usa igual que siempre, sin notar el vigilante |
| VIG.2 | Trabaja unos minutos con lo más pesado: exportar un STL de una pieza con rosca, abrir **Exportar** con varios cuerpos y marcar y desmarcar, importar un STEP, **Medir** sobre una rosca, abrir un generador con texto. Cierra la aplicación | Cierra con normalidad |
| VIG.3 | Abre `%APPDATA%\NufioCAD\NufioCAD\cierres\paradas.txt` | Hay una línea `=== Sesión … · umbral 200 ms ===` y, por cada parada, la hora, `parada de N ms` y (Windows) las funciones donde estaba. Las paradas que tú notaste aparecen |
| VIG.4 | Repite con `$env:NUF_VIGILANTE = 500` | La sesión dice `umbral 500 ms` y solo salen las paradas de medio segundo o más |
| VIG.5 | Abre la aplicación **sin** la variable | No se escribe nada nuevo en `paradas.txt` |

El informe se acumula entre sesiones; bórralo cuando quieras empezar de cero. Mientras se arrastra o se redimensiona la ventana, Windows puede retrasar un instante los eventos y salir alguna parada falsa y corta.

### 5.5 Llamadas en segundo plano: exportar, importar, medir, recuperar (`FLU`)

Fase 13, F2. Todas estas acciones ya no deben dejar la ventana sin responder. Usa una pieza con rosca (Sólido › Rosca sobre un cilindro) o un engranaje de muchos dientes para que tarden.

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| FLU.1 | Archivo › Exportar, STL, calidad Alta; mientras escribe, gira la vista y mueve el ratón | La ventana responde; la barra dice «Exportando a …» y al terminar «Exportado a …». El archivo abre bien en el slicer |
| FLU.2 | Lanza una exportación y, sin esperar, otra | La segunda dice «Ya se está exportando; espera a que termine» |
| FLU.3 | Abre Exportar con varios cuerpos; marca y desmarca cuerpos y cambia la calidad (Baja, Media, Alta) | Aparece «Contando los triángulos…» y luego «Unos N triángulos». Marcar y desmarcar no congela el diálogo; repetir una combinación ya contada es instantáneo |
| FLU.4 | En ese diálogo, cambia la boquilla o la cama | «Revisando la pieza para imprimir…» y después el resultado, sin congelar |
| FLU.5 | Archivo › Imprimir con un slicer configurado | «Preparando el STL para el slicer…» y luego se abre el slicer con el archivo |
| FLU.6 | Importa un STEP grande (de otro programa); mientras importa, mueve la vista | La ventana responde y dice «Importando …»; al terminar aparece la ficha «Importación» y el cuerpo, igual que antes |
| FLU.7 | Importa un STEP y, antes de que termine, abre otro diseño (Archivo › Nuevo o Abrir) | Al terminar avisa «Se descartó la importación…» y no añade nada al diseño nuevo. Otra prueba: con un sketch abierto al terminar, avisa que lo cierres y vuelvas a importar |
| FLU.8 | Sólido › Medir; elige una cara de una rosca y luego otra, rápido | El panel dice «midiendo…» y luego el resultado de la **última** selección; no se cuelga |
| FLU.9 | `NUF_RECOVERY_SECONDS=5`, abre un diseño con un STEP importado grande, edita algo y espera varios ciclos | No hay tirón cada 5 s. Mata el proceso: al abrir ofrece restaurar. Con un cierre normal no queda ninguna copia en `recuperacion` |
| FLU.10 | Guarda 10 diseños distintos y reinicia con la pantalla de inicio | La lista sale al instante (con «Cargando…») y las miniaturas aparecen de una en una |
| FLU.11 | Con OneDrive «archivos a petición», que un reciente esté solo en la nube | Sale como «Solo en la nube» y no se descarga para dibujar la lista |

### 5.6 Dibujo en segundo plano: mallas, vista previa y generadores (`FLU`, continuación)

Fase 13, F3. Los cuerpos nuevos ya no se dibujan hasta tener su malla (se prepara aparte): mientras tanto se sigue viendo lo anterior, y la ventana responde. Usa un par de engranajes, una rosca o un patrón de muchas copias.

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| FLU.12 | Herramientas › Par de engranajes (o Engranaje interno); cambia el módulo o el número de dientes varias veces seguidas | La ventana responde (se puede girar la vista); la vista previa azul se actualiza al terminar de calcular, sin parpadear ni mostrar piezas a medias |
| FLU.13 | Abre un generador con la vista vacía | La vista previa **queda encuadrada** (se ve entera, sin salirse por los bordes), igual que antes |
| FLU.14 | Abre un diseño guardado con varias piezas; con el diseño ya abierto, Archivo › Abrir otro | Tras abrirlo, la vista queda **encuadrada** sobre todas las piezas |
| FLU.15 | Extruir (o Agujero) en modo «Cortar» sobre una pieza; cambia la distancia | El volumen que se quita aparece en **rojo translúcido** poco después del cuerpo, sin congelar la ventana; al aceptar sale lo mismo que se veía |
| FLU.16 | Letrero o llavero: cambia el texto y la fuente; acepta | El diseño aceptado es idéntico a la vista previa; aceptar no tarda más que antes (la receta de la vista previa se reutiliza) |
| FLU.17 | Con piezas ya hechas (una caja, un cilindro, un engranaje), compara el aspecto con la versión anterior (`develop`) | Mismo sombreado, **misma línea de borde entre cada par de caras** (también en los cilindros) y misma suavidad de curvas; las roscas igual de lisas |

Si la vista previa de una herramienta tarda más de 150 ms en dibujarse, en la barra de estado debe aparecer «Preparando el dibujo de la pieza…» y desaparecer al terminar; con piezas sencillas no debe parpadear.

### 5.7 Indicador de carga y cierre con trabajo en marcha (`FLU`, continuación)

Fase 13, F4. Mientras algo carga, la barra de estado se pone **ámbar** y gira un **spinner**; al terminar, vuelve al azul.

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| FLU.18 | Crea un cilindro y una rosca cortante encima; cambia el diámetro del cilindro | La barra de abajo se pone **ámbar** con un spinner girando en su parte derecha, y sobre la vista sale «Calculando…» con otro spinner. Al terminar todo vuelve al **azul** y los spinners desaparecen |
| FLU.19 | Exporta una pieza con rosca (Archivo › Exportar) | Mientras escribe, banner ámbar y spinner; al terminar, azul y el mensaje «Exportado a …» |
| FLU.20 | Importa un STEP grande | Lo mismo mientras importa |
| FLU.21 | Con un patrón de 50 copias recién agregado (tarda), cambia algo antes de que termine | El cálculo anterior se abandona; el banner sigue ámbar hasta que termina el último; nunca queda ámbar con todo parado |
| FLU.22 | Con una rosca larga calculándose (cilindro y rosca de largo 80), cierra la ventana | La ventana **desaparece al instante**; el proceso se va en un par de segundos como mucho (antes esperaba a que terminara la rosca) |
| FLU.23 | Edita varias veces seguidas el diámetro de un cilindro con una rosca encima | Solo vale el último; nunca se ve un resultado viejo después de uno nuevo |

Los colores: azul `#0B5CAD` en reposo y ámbar `#A85A00` mientras carga; el parpadeo al cambiar el mensaje se aclara con el color de cada estado. Si el spinner te molesta, es un indicador pequeño (16 px) y solo gira mientras se ve.

### 5.8 Calcular menos: patrones, nombres, refresco y arrastre del sketch (`FLU`, continuación)

Fase 13, F5. Estas pruebas buscan que lo de siempre siga igual (mismos resultados) y que ahora sea más rápido.

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| FLU.24 | Crea una caja y un **patrón lineal de 50 copias** (Sólido › Patrón), con una distancia mayor que la caja (separadas) y luego menor (solapadas) | Sale la misma pieza de siempre en ambos casos (las solapadas, unidas en una sola); tarda mucho menos que antes. Guárdalo, ábrelo y comprueba que un sketch sobre una cara de una copia sigue funcionando |
| FLU.25 | Une un cuerpo con varios que toca (extruir en «Unir» sobre dos cuerpos) | Quedan unidos como antes, con los nombres de las caras estables: un sketch sobre una de sus caras sigue enganchado tras editar una operación anterior |
| FLU.26 | Abre la aplicación y, **sin esperar**, abre el primer generador (Lightbox) | No hay parón al abrirlo (la búsqueda de fuentes se hace sola al abrir la ventana; la propia ventana puede tardar hasta un segundo más en quedar fluida nada más arrancar) |
| FLU.27 | Agrega un sketch, extruye, deshaz y rehaz varias veces | El navegador y el timeline se actualizan bien en cada paso y no queda nada a medias; con el cálculo en segundo plano, al terminar aparece el resultado y el navegador está al día |
| FLU.28 | En un sketch con muchas figuras (50 o más líneas cerradas formando regiones), arrastra un punto | El sombreado de las regiones se actualiza unas diez veces por segundo mientras arrastras y queda **exacto** al soltar; el arrastre no se siente más lento |

### 5.9 Memoria al exportar (`FLU`, continuación)

Fase 13, F6. Mismos archivos que antes, con menos memoria.

| ID | Qué hacer | Qué debería pasar |
|---|---|---|
| FLU.29 | Exporta a STL, OBJ y STEP una pieza con muchas curvas (un engranaje de 100 dientes helicoidal) en calidad **Alta**, mirando el uso de memoria en el Administrador de tareas | La exportación termina bien; los archivos abren en tu slicer, **cerrados, en mm y con el mismo aspecto que antes**; la memoria sube menos que antes |
| FLU.30 | Abre Exportar, elige calidad Alta con varios cuerpos y mira «Unos N triángulos» | El número sale igual que antes (misma cuenta) y el diálogo no se queda sin responder |

### 5.2 Letrero con fuentes decorativas

1. Generadores → Letrero o llavero, texto «Nufio», fuente **Ravie** (o Jokerman, Curlz, etc.).
2. Debe aparecer la placa con las letras; si algo falla, el panel muestra el motivo en rojo (nunca en blanco).

### 5.3 Pruebas transversales (cuando tengas tiempo)

1. **Flujo completo**: diseña algo tuyo (por ejemplo una carcasa con tapa y tornillos) usando varias fases a la vez; guarda, cierra, abre, edita algo del principio del historial.
2. **Estrés**: sketches con muchas figuras, piezas con 10+ operaciones, patrones de 20+ copias. Anota qué se siente lento.
3. **Errores**: intenta romper cosas (radios enormes, borrar cosas de las que dependen otras, abrir un archivo vacío renombrado a `.nuf`).
4. **Impresión 3D**: exporta STL de varias piezas y ábrelas en tu slicer (¿cerradas, en mm, sin caras invertidas?).
5. **Claridad**: ¿qué cosa te costó descubrir sin ayuda? Eso es feedback de UX muy valioso.

---

## Plantilla para darme feedback

```
Sección / prueba:      (por ejemplo ACA.6 Agujero)
Qué hice:
Qué esperaba:
Qué pasó:
Tipo:               [ ] error  [ ] se ve mal  [ ] confuso / poco intuitivo  [ ] lento  [ ] falta algo  [ ] me gusta
Gravedad:           [ ] bloquea  [ ] molesta  [ ] detalle
Captura / archivo .nuf (opcional):
```

Con una captura de pantalla y los pasos suele bastar. Si algo se cierra, no hace falta que copies nada: el informe queda guardado y lo leo yo.

---

## Equivalencias con los IDs anteriores

Los IDs antiguos se repetían entre secciones (por ejemplo había tres `R.1`); la tabla dice a qué prueba nueva corresponde cada uno, junto con el nombre que tenía su sección. Los documentos viejos (hoja de ruta, registro de cambios) siguen usando los IDs anteriores.

| Antes | Sección anterior | Ahora |
|---|---|---|
| `1.1` | Fase 1 | `NAV.1` |
| `1.2` | Fase 1 | `NAV.2` |
| `1.3` | Fase 1 | `NAV.3` |
| `1.4` | Fase 1 | `NAV.4` |
| `1.5` | Fase 1 | `NAV.5` |
| `1.6` | Fase 1 | `NAV.6` |
| `1.7` | Fase 1 | `NAV.7` |
| `I.1` | Interfaz general | `UI.1` |
| `I.2` | Interfaz general | `UI.2` |
| `I.3` | Interfaz general | `UI.3` |
| `I.4` | Interfaz general | `UI.4` |
| `I.5` | Interfaz general | `UI.5` |
| `2.A` | Fase 2 (añadidas) | `SKD.1` |
| `2.B` | Fase 2 (añadidas) | `SKD.2` |
| `2.C` | Fase 2 (añadidas) | `SKD.3` |
| `H.1` | Extruir hasta una cara curva | `EXT.1` |
| `H.2` | Extruir hasta una cara curva | `EXT.2` |
| `H.3` | Extruir hasta una cara curva | `EXT.3` |
| `H.4` | Extruir hasta una cara curva | `EXT.4` |
| `H.5` | Extruir hasta una cara curva | `EXT.5` |
| `H.6` | Extruir hasta una cara curva | `EXT.6` |
| `H.7` | Extruir hasta una cara curva | `EXT.7` |
| `H.8` | Extruir hasta una cara curva | `EXT.8` |
| `4.A` | Fase 4 | `ARC.1` |
| `4.B` | Fase 4 | `ARC.2` |
| `4.C` | Fase 4 | `ARC.3` |
| `4.D` | Fase 4 | `ARC.4` |
| `5.1` | Fase 5 | `ACA.1` |
| `5.2` | Fase 5 | `ACA.2` |
| `5.3` | Fase 5 | `ACA.3` |
| `5.4` | Fase 5 | `ACA.4` |
| `5.5` | Fase 5 | `ACA.5` |
| `5.6` | Fase 5 | `ACA.6` |
| `5.7` | Fase 5 | `ACA.7` |
| `5.8` | Fase 5 | `ACA.8` |
| `5.9` | Fase 5 | `ACA.9` |
| `5.10` | Fase 5 | `ACA.10` |
| `5.11` | Fase 5 | `ACA.11` |
| `5.12` | Fase 5 | `ACA.12` |
| `5.13` | Fase 5 | `ACA.13` |
| `5.14` | Fase 5 | `ACA.14` |
| `F.1` | Flecha de la medida | `FLE.1` |
| `F.2` | Flecha de la medida | `FLE.2` |
| `F.3` | Flecha de la medida | `FLE.3` |
| `F.4` | Flecha de la medida | `FLE.4` |
| `F.5` | Flecha de la medida | `FLE.5` |
| `F.6` | Flecha de la medida | `FLE.6` |
| `F.8` | Flecha de la medida | `FLE.7` |
| `F.9` | Flecha de la medida | `FLE.8` |
| `F.11` | Flecha de la medida | `FLE.9` |
| `F.12` | Flecha de la medida | `FLE.10` |
| `F.13` | Flecha de la medida | `FLE.11` |
| `F.14` | Flecha de la medida | `FLE.12` |
| `F.15` | Flecha de la medida | `FLE.13` |
| `F.16` | Flecha de la medida | `FLE.14` |
| `F.10` | Flecha de la medida | `FLE.15` |
| `F.7` | Flecha de la medida | `FLE.16` |
| `6.1` | Fase 6 | `REF.1` |
| `6.2` | Fase 6 | `REF.2` |
| `6.3` | Fase 6 | `REF.3` |
| `6.4` | Fase 6 | `REF.4` |
| `6.5` | Fase 6 | `REF.5` |
| `6.6` | Fase 6 | `REF.6` |
| `6.7` | Fase 6 | `REF.7` |
| `6.8` | Fase 6 | `REF.8` |
| `6.9` | Fase 6 | `REF.9` |
| `6.10` | Fase 6 | `REF.10` |
| `6.11` | Fase 6 | `REF.11` |
| `6.12` | Fase 6 | `REF.12` |
| `6.13` | Fase 6 | `REF.13` |
| `6.14` | Fase 6 | `REF.14` |
| `6.15` | Fase 6 | `REF.15` |
| `6.16` | Fase 6 | `REF.16` |
| `6.17` | Fase 6 | `REF.17` |
| `6.18` | Fase 6 | `REF.18` |
| `6.19` | Fase 6 | `REF.19` |
| `6.20` | Fase 6 | `REF.20` |
| `6.21` | Fase 6 | `REF.21` |
| `6.22` | Fase 6 | `REF.22` |
| `6.23` | Fase 6 | `REF.23` |
| `6.24` | Fase 6 | `REF.24` |
| `7.1` | Fase 7 | `GEN.1` |
| `7.2` | Fase 7 | `GEN.2` |
| `7.3` | Fase 7 | `GEN.3` |
| `7.4` | Fase 7 | `GEN.4` |
| `7.5` | Fase 7 | `GEN.5` |
| `7.6` | Fase 7 | `GEN.6` |
| `7.7` | Fase 7 | `GEN.7` |
| `7.8` | Fase 7 | `GEN.8` |
| `7.9` | Fase 7 | `GEN.9` |
| `7.10` | Fase 7 | `GEN.10` |
| `7.11` | Fase 7 | `GEN.11` |
| `7.12` | Fase 7 | `GEN.12` |
| `8.1` | Fase 8 | `MUL.1` |
| `8.2` | Fase 8 | `MUL.2` |
| `8.3` | Fase 8 | `MUL.3` |
| `8.4` | Fase 8 | `MUL.4` |
| `8.5` | Fase 8 | `MUL.5` |
| `8.6` | Fase 8 | `MUL.6` |
| `8.7` | Fase 8 | `MUL.7` |
| `8.8` | Fase 8 | `MUL.8` |
| `8.9` | Fase 8 | `MUL.9` |
| `8.10` | Fase 8 | `MUL.10` |
| `8.11` | Fase 8 | `MUL.11` |
| `8.12` | Fase 8 | `MUL.12` |
| `8.13` | Fase 8 | `MUL.13` |
| `8.14` | Fase 8 | `MUL.14` |
| `8.15` | Fase 8 | `MUL.15` |
| `8.16` | Fase 8 | `MUL.16` |
| `8.17` | Fase 8 | `MUL.17` |
| `8.18` | Fase 8 | `MUL.18` |
| `8.19` | Fase 8 | `MUL.19` |
| `9.1` | Fase 9 | `ARC.6` |
| `9.2` | Fase 9 | `ARC.7` |
| `9.3` | Fase 9 | `ARC.8` |
| `9.4` | Fase 9 | `ARC.9` |
| `9.5` | Fase 9 | `ARC.10` |
| `9.6` | Fase 9 | `ARC.11` |
| `9.7` | Fase 9 | `ARC.12` |
| `9.8` | Fase 9 | `ARC.13` |
| `9.9` | Fase 9 | `ARC.14` |
| `9.10` | Fase 9 | `ARC.15` |
| `9.11` | Fase 9 | `ARC.16` |
| `9.12` | Fase 9 | `ARC.17` |
| `9.13` | Fase 9 | `ARC.18` |
| `9.14` | Fase 9 | `INS.1` |
| `9.15` | Fase 9 | `INS.2` |
| `9.16` | Fase 9 | `INS.3` |
| `9.17` | Fase 9 | `INS.4` |
| `9.18` | Fase 9 | `INS.5` |
| `9.19` | Fase 9 | `INS.6` |
| `9.20` | Fase 9 | `INS.7` |
| `9.21` | Fase 9 | `INS.8` |
| `9.22` | Fase 9 | `INS.9` |
| `9.23` | Fase 9 | `INS.10` |
| `9.24` | Fase 9 | `INS.11` |
| `U.1` | Unidades | `UNI.1` |
| `U.2` | Unidades | `UNI.2` |
| `U.3` | Unidades | `UNI.3` |
| `U.4` | Unidades | `UNI.4` |
| `U.5` | Unidades | `UNI.5` |
| `U.6` | Unidades | `UNI.6` |
| `U.7` | Unidades | `UNI.7` |
| `U.8` | Unidades | `UNI.8` |
| `U.9` | Unidades | `UNI.9` |
| `R.1` | Rosca interior y colocación | `GEN.13` |
| `R.2` | Rosca interior y colocación | `GEN.14` |
| `R.3` | Rosca interior y colocación | `GEN.15` |
| `R.4` | Rosca interior y colocación | `GEN.16` |
| `R.5` | Rosca interior y colocación | `GEN.17` |
| `R.6` | Rosca interior y colocación | `GEN.18` |
| `R.7` | Rosca interior y colocación | `GEN.19` |
| `G.1` | Lightbox | `LIG.1` |
| `G.2` | Lightbox | `LIG.2` |
| `G.3` | Lightbox | `LIG.3` |
| `G.4` | Lightbox | `LIG.4` |
| `G.5` | Lightbox | `LIG.5` |
| `G.6` | Lightbox | `LIG.6` |
| `G.7` | Lightbox | `LIG.7` |
| `G.8` | Lightbox | `LIG.8` |
| `G.9` | Lightbox | `LIG.9` |
| `G.10` | Lightbox | `LIG.10` |
| `G.11` | Lightbox | `LIG.11` |
| `G.12` | Lightbox | `GRE.1` |
| `G.13` | Lightbox | `GRE.2` |
| `G.14` | Lightbox | `GRE.3` |
| `G.15` | Lightbox | `GRE.4` |
| `G.17` | Lightbox | `GRE.6` |
| `G.18` | Lightbox | `GRE.7` |
| `G.16` | Lightbox | `GRE.5` |
| `C.1` | Caja con tapa | `CAJ.1` |
| `C.2` | Caja con tapa | `CAJ.2` |
| `C.3` | Caja con tapa | `CAJ.3` |
| `C.4` | Caja con tapa | `CAJ.4` |
| `C.5` | Caja con tapa | `CAJ.5` |
| `T.1` | Letrero o llavero | `LET.1` |
| `T.2` | Letrero o llavero | `LET.2` |
| `T.3` | Letrero o llavero | `LET.3` |
| `T.4` | Letrero o llavero | `LET.4` |
| `T.5` | Letrero o llavero | `LET.5` |
| `T.6` | Letrero o llavero | `LET.6` |
| `T.7` | Letrero o llavero | `LET.7` |
| `E.1` | Par de engranajes | `PAR.1` |
| `E.2` | Par de engranajes | `PAR.2` |
| `E.3` | Par de engranajes | `PAR.3` |
| `E.4` | Par de engranajes | `PAR.4` |
| `E.5` | Par de engranajes | `PAR.5` |
| `E.6` | Par de engranajes | `PAR.6` |
| `E.7` | Engranaje interno y cremallera | `PAR.7` a `PAR.17` |
| `B.1` | Cálculo en segundo plano | `CAL.1` |
| `B.2` | Cálculo en segundo plano | `CAL.2` |
| `B.3` | Cálculo en segundo plano | `CAL.3` |
| `B.4` | Cálculo en segundo plano | `CAL.4` |
| `B.5` | Cálculo en segundo plano | `CAL.5` |
| `B.6` | Cálculo en segundo plano | `CAL.6` |
| `B.7` | Cálculo en segundo plano | `CAL.7` |
| `B.8` | Cálculo en segundo plano | `CAL.8` |
| `M.1` | Errores de geometría | `ERR.1` |
| `M.2` | Errores de geometría | `ERR.2` |
| `M.3` | Errores de geometría | `ERR.3` |
| `M.4` | Errores de geometría | `ERR.4` |
| `C.1` | Cotas sobre el lienzo | `SKC.1` |
| `C.2` | Cotas sobre el lienzo | `SKC.2` |
| `C.3` | Cotas sobre el lienzo | `SKC.3` |
| `C.4` | Cotas sobre el lienzo | `SKC.4` |
| `C.5` | Cotas sobre el lienzo | `SKC.5` |
| `C.6` | Cotas sobre el lienzo | `SKC.6` |
| `C.7` | Cotas sobre el lienzo | `SKC.7` |
| `C.8` | Cotas sobre el lienzo | `SKC.8` |
| `C.9` | Cotas sobre el lienzo | `SKC.9` |
| `C.10` | Cotas sobre el lienzo | `SKC.10` |
| `C.11` | Cotas sobre el lienzo | `SKC.11` |
| `C.12` | Cotas sobre el lienzo | `SKC.12` |
| `C.13` | Cotas sobre el lienzo | `SKC.13` |
| `C.14` | Cotas sobre el lienzo | `SKC.14` |
| `C.15` | Cotas sobre el lienzo | `SKC.15` |
| `C.16` | Cotas sobre el lienzo | `SKC.16` |
| `I.1` | Alineaciones con línea naranja | `SKI.1` |
| `I.2` | Alineaciones con línea naranja | `SKI.2` |
| `I.3` | Alineaciones con línea naranja | `SKI.3` |
| `I.4` | Alineaciones con línea naranja | `SKI.4` |
| `I.5` | Alineaciones con línea naranja | `SKI.5` |
| `I.6` | Alineaciones con línea naranja | `SKI.6` |
| `I.7` | Alineaciones con línea naranja | `SKI.7` |
| `I.8` | Alineaciones con línea naranja | `SKI.8` |
| `I.9` | Alineaciones con línea naranja | `SKI.9` |
| `I.10` | Alineaciones con línea naranja | `SKI.10` |
| `I.12` | Alineaciones con línea naranja | `SKI.19` |
| `I.13` | Alineaciones con línea naranja | `SKI.20` |
| `I.14` | Alineaciones con línea naranja | `SKI.21` |
| `I.15` | Alineaciones con línea naranja | `SKI.22` |
| `I.16` | Alineaciones con línea naranja | `SKI.23` |
| `I.11` | Alineaciones con línea naranja | `SKI.11` |
| `R.1` | Iconos de restricciones | `SKR.2` |
| `R.2` | Iconos de restricciones | `SKR.3` |
| `R.3` | Iconos de restricciones | `SKR.4` |
| `R.4` | Iconos de restricciones | `SKR.5` |
| `Q.1` | Cotas estables | `SKC.28` |
| `Q.2` | Cotas estables | `SKC.29` |
| `Q.4` | Cotas estables | `SKI.27` |
| `Q.3` | Cotas estables | `SKI.24` |
| `P.1` | Auditoría del sketch | `SKP.1` |
| `P.2` | Auditoría del sketch | `SKP.2` |
| `P.3` | Auditoría del sketch | `SKP.3` |
| `O.1` | Ayuda en las herramientas | `AYU.1` |
| `O.2` | Ayuda en las herramientas | `AYU.2` |
| `O.3` | Ayuda en las herramientas | `AYU.3` |
| `O.4` | Ayuda en las herramientas | `AYU.4` |
| `N.1` | Pendientes del sketch | `SKC.27` |
| `N.2` | Pendientes del sketch | `SKI.16` |
| `N.3` | Pendientes del sketch | `SKI.17` |
| `N.4` | Pendientes del sketch | `SKI.18` |
| `M.1` | Figuras abiertas | `SKF.1` |
| `M.2` | Figuras abiertas | `SKF.2` |
| `M.3` | Figuras abiertas | `SKF.3` |
| `M.4` | Figuras abiertas | `SKF.4` |
| `M.5` | Figuras abiertas | `SKF.5` |
| `M.6` | Figuras abiertas | `SKF.6` |
| `M.7` | Figuras abiertas | `SKF.7` |
| `M.8` | Figuras abiertas | `SKF.8` |
| `M.9` | Figuras abiertas | `SKF.9` |
| `M.10` | Figuras abiertas | `SKF.13` |
| `L.1` | Tangente e histéresis | `SKI.25` |
| `L.2` | Tangente e histéresis | `SKI.26` |
| `L.3` | Tangente e histéresis | `SKI.15` |
| `J.1` | Ajustes tras la primera prueba | `SKD.4` |
| `J.2` | Ajustes tras la primera prueba | `SKI.12` |
| `J.3` | Ajustes tras la primera prueba | `SKI.13` |
| `J.4` | Ajustes tras la primera prueba | `SKI.14` |
| `J.5` | Ajustes tras la primera prueba | `ARC.5` |
| `J.6` | Ajustes tras la primera prueba | `SKC.25` |
| `J.7` | Ajustes tras la primera prueba | `SKC.26` |
| `J.8` | Ajustes tras la primera prueba | `SKD.5` |
| `K.1` | Cotas que no se pisan | `SKC.17` |
| `K.2` | Cotas que no se pisan | `SKC.18` |
| `K.3` | Cotas que no se pisan | `SKC.19` |
| `K.4` | Cotas que no se pisan | `SKC.20` |
| `K.5` | Cotas que no se pisan | `SKC.21` |
| `K.6` | Cotas que no se pisan | `SKC.22` |
| `K.7` | Cotas que no se pisan | `SKC.23` |
| `K.8` | Cotas que no se pisan | `SKC.24` |
| `O.1` | Mover y girar arrastrando | `MOV.1` |
| `O.2` | Mover y girar arrastrando | `MOV.2` |
| `O.3` | Mover y girar arrastrando | `MOV.3` |
| `O.4` | Mover y girar arrastrando | `MOV.4` |
| `O.5` | Mover y girar arrastrando | `MOV.5` |
| `O.5d` | Mover y girar arrastrando | `MOV.6` |
| `O.5f` | Mover y girar arrastrando | `MOV.7` |
| `O.5e` | Mover y girar arrastrando | `MOV.8` |
| `O.5c` | Mover y girar arrastrando | `MOV.9` |
| `O.5b` | Mover y girar arrastrando | `MOV.10` |
| `O.6` | Mover y girar arrastrando | `MOV.11` |
| `O.7` | Mover y girar arrastrando | `MOV.12` |
| `O.8` | Mover y girar arrastrando | `MOV.13` |
| `O.9` | Mover y girar arrastrando | `MOV.14` |
| `O.10` | Mover y girar arrastrando | `MOV.15` |
| `O.10b` | Mover y girar arrastrando | `MOV.16` |
| `O.11` | Mover y girar arrastrando | `MOV.17` |
| `O.12` | Mover y girar arrastrando | `MOV.18` |
| `O.13` | Mover y girar arrastrando | `MOV.19` |
| `O.14` | Mover y girar arrastrando | `MOV.20` |
| `O.15` | Mover y girar arrastrando | `MOV.21` |
| `O.16` | Mover y girar arrastrando | `MOV.22` |
| `O.17` | Mover y girar arrastrando | `MOV.23` |
| `O.18` | Mover y girar arrastrando | `MOV.24` |
| `R.1` | Restablecer valores | `GRE.8` |
| `R.2` | Restablecer valores | `GRE.9` |
| `R.3` | Restablecer valores | `GRE.10` |
| `T.1` | Vista previa translúcida | `VPR.1` |
| `T.2` | Vista previa translúcida | `VPR.2` |
| `T.3` | Vista previa translúcida | `VPR.3` |
| `T.4` | Vista previa translúcida | `VPR.4` |
| `T.5` | Vista previa translúcida | `VPR.5` |
