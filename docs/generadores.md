# Herramientas generadoras: medidas y convenciones

Qué produce cada generador (pestaña **HERRAMIENTAS** o menú **Herramientas**), con todas sus medidas, los nombres que
le pone a las cosas y cómo se relaciona cada parámetro con la geometría. El estado de cada generador y lo que falta
está en [`hoja-de-ruta.md`](hoja-de-ruta.md) (Fase 10) y cómo probarlos, en
[`guia-de-pruebas.md`](guia-de-pruebas.md).

## Familias de herramientas

La pestaña **HERRAMIENTAS** y el menú Herramientas agrupan los generadores por familia (cada uno declara la suya con
`Generator::category()`):

| Familia | Generadores |
|---|---|
| **Cajas** | Lightbox, Caja con tapa, Bote con tapa |
| **Letreros** | Letrero o llavero |
| **Engranajes** | Par de engranajes, Engranaje interno, Cremallera, Tornillo sin fin y rueda, Engranajes cónicos, Tren planetario |

Un generador nuevo elige su familia (o crea una); las familias van seguidas en la lista.

## Cómo funciona un generador

1. Hace unas preguntas (el formulario del panel de la derecha); las medidas se escriben en la unidad elegida en
   Archivo › Configuración (el diseño siempre se guarda en milímetros).
2. Muestra una **vista previa en vivo** del diseño (se recalcula 250 ms después del último cambio).
3. Al aceptar, crea de una vez (un solo Ctrl+Z lo deshace):
   - **Parámetros con nombre** en Sólido › Parámetros (`ancho`, `pared`…), con el valor que pusiste.
   - **Operaciones normales** en el historial («Lightbox: caja», «Lightbox: hueco»…), cada una ligada a esos
     parámetros mediante expresiones: al cambiar un parámetro se recalcula todo el diseño.
   - **Cuerpos con nombre** agrupados en un **componente** (Navegador).
4. Si el diseño ya tiene parámetros con esos nombres (un segundo Lightbox), los nuevos llevan un sufijo: `ancho_2`,
   `pared_2`… y no pisan a los primeros.

Después se puede seguir editando con el resto del CAD: doble clic en una operación del historial abre su panel,
y Sólido › Parámetros cambia las medidas de todo el diseño. El formulario del generador no se vuelve a abrir.

## Convenciones geométricas

| Qué | Cómo |
|---|---|
| Unidades | milímetros (se muestran en la unidad configurada) |
| Plano de trabajo | XY, con la pieza centrada en el origen |
| Ancho y alto | a lo largo de X y de Y |
| Profundidad | a lo largo de Z: el **fondo** de la caja está en Z = 0 (apoyado en la cama) y la caja se abre por **delante** (+Z) |
| Tapa | sale **al lado** de la caja, en +X a `ancho + 10` mm del centro, con la cara de la imagen apoyada en Z = 0 (cara abajo, como se imprime) |
| Cuerpos | «Caja» y «Tapa» (un solo cuerpo), en el componente «Lightbox» |

## Lightbox (rectangular y redonda)

Hecha a imagen de la «Light Box» de MakerLab (ver la referencia al final de esta sección): una **caja** abierta
por delante (fondo cerrado y paredes) y una **tapa delantera** que entra en la caja. La tapa es **un solo cuerpo**: la placa (el marco y la cara central
—la zona de la imagen—, sin juntas) con el **labio** (un anillo que entra en la caja) unido a ella. Así hay solo dos
cuerpos que manejar. Ya no se puede dar un filamento distinto a la cara central.

### Preguntas y medidas

| Pregunta | Parámetro | Por defecto | Mín. – máx. | Para qué sirve |
|---|---|---|---|---|
| Forma | — | Rectangular | Rectangular / Redonda | Planta rectangular o circular |
| Ancho (exterior) | `ancho` | 175 mm | 30 – 1000 | Ancho exterior de la caja (la imagen mide esto − 2·pared) |
| Alto (exterior) | `alto` | 131 mm | 30 – 1000 | Alto exterior |
| Diámetro (exterior) | `diametro` | 150 mm | 30 – 1000 | Solo en la redonda |
| Profundidad total | `profundidad` | 37 mm | 10 – 500 | Del fondo hasta el borde delantero (fondo incluido; el hueco mide profundidad − fondo) |
| Pared | `pared` | 2 mm | 0,8 – 20 | Grosor de las paredes; también el ancho del marco |
| Fondo de la caja | `fondo` | 2 mm | 0,8 – 20 | Grosor de la pared de atrás |
| Frente (marco e imagen) | `frente` | 0,8 mm | 0,4 – 10 | Grosor del marco y de la cara central: más fino, más luz pasa |
| Radio de esquinas | `esquina` | 2 mm | 0 – 30 | Esquinas exteriores redondeadas de la caja y del marco (rectangular) |
| Alto del labio | `labio` | 5 mm | 1 – 30 | Cuánto entra la tapa en la caja |
| Grosor del labio | `labio_pared` | 1 mm | 0,4 – 10 | Grosor del anillo del labio |
| Holgura del labio | `holgura` | 0,1 mm | 0 – 2 | Aire entre el labio y la pared, por cada lado (la referencia de MakerLab usa 0) |
| Agujero del cable | `cable` | 6 mm | 0 – 50 | Diámetro del agujero; 0 = sin agujero |
| Salida del cable | — | Atrás (en el fondo) | ver abajo | Por dónde sale el cable |

Para una boquilla de 0,4 mm: pared y fondo como múltiplos del ancho de línea (2 mm = 5 líneas de 0,4); el frente de 0,8
mm son 4 capas de 0,2 (o 5 de 0,16); una holgura de 0,1 a 0,2 mm suele bastar para que el labio entre sin forzar.

### Salida del cable

| Opción | Dónde hace el agujero |
|---|---|
| Atrás (en el fondo) — por defecto | Atraviesa el fondo de la caja, centrado en X y a un cuarto del alto por debajo del centro |
| Pared de abajo | Atraviesa la pared inferior (Y negativo), centrado en X y a media altura del hueco |
| Pared de arriba | Igual, por la pared superior (Y positivo) |
| Pared izquierda | Atraviesa la pared de X negativo, centrada en Y y a media altura del hueco |
| Pared derecha | Igual, por la pared de X positivo |

A media altura del hueco quiere decir en Z = `fondo + (profundidad − fondo) / 2`. En la redonda, las cuatro paredes son
la misma pared curva (abajo, arriba, izquierda y derecha son direcciones del círculo).

### Medidas derivadas (con los valores por defecto)

| Pieza | Medida |
|---|---|
| Caja, exterior | 175 × 131 × 37 mm, esquinas de radio 2 |
| Hueco | 171 × 127 mm (ancho − 2·pared, alto − 2·pared) y 35 mm de profundidad, esquinas interiores vivas |
| Placa de la tapa | 175 × 131 × 0,8 mm (marco y cara central, un solo sólido) |
| Labio | Anillo de 170,8 × 126,8 exterior (hueco − 2·holgura) y 168,8 × 124,8 interior, 5 mm de alto, unido a la placa (Z de 0,8 a 5,8) |
| Volumen de la caja sin agujero | ≈ 88 000 mm³ |

La versión redonda usa las mismas fórmulas con círculos (diámetro exterior, hueco de diámetro − 2·pared, labio de
diámetro − 2·pared − 2·holgura).

### Operaciones que crea (rectangular)

| # | Nombre | Tipo | Medidas (expresiones) | Operación |
|---|---|---|---|---|
| 1 | Lightbox: caja | Caja centrada, radio `esquina` | ancho × alto × profundidad, desde Z = 0 | Cuerpo nuevo |
| 2 | Lightbox: hueco | Caja centrada, desplazada `fondo` en Z | (ancho − 2·pared) × (alto − 2·pared) × profundidad | Cortar |
| 3 | Lightbox: agujero del cable | Cilindro Ø `cable` por el fondo o por la pared elegida | por el fondo: desde Z = −1, longitud `fondo + 2`; por una pared: `pared + 2`, repartida a los dos lados | Cortar |
| 4 | Lightbox: tapa | Caja centrada en X = ancho + 10, radio `esquina` | ancho × alto × frente | Cuerpo nuevo |
| 5 | Lightbox: labio | Caja en el mismo sitio, desde Z = frente − 0,01 | (hueco − 2·holgura) × (alto del labio + 0,01) | Unir (a la tapa) |
| 6 | Lightbox: hueco del labio | Caja en el mismo sitio, desde Z = frente | (hueco − 2·holgura − 2·grosor del labio), más alta que el labio | Cortar |

En la redonda, las cajas son cilindros (el diámetro hace de ancho y de alto) y no hay radio de esquinas.

### Nombres

- Cuerpos: **Caja** y **Tapa**; componente **Lightbox**.
- Operaciones: las de la tabla («Lightbox: …»).
- Caras: cada caja y cada cilindro nombra sus caras por su orientación (ver más abajo; las esquinas redondeadas se
  llaman `esquina+x+y`, etc.), así que se puede dibujar un sketch en la **cara de la tapa** (para cortar o dar relieve a tu
  imagen) y la operación sigue su cara si cambias una medida.

### Referencia: la «Light Box» de MakerLab

Medida a partir de los dos proyectos de ejemplo (`Fam_Nufio_Illescas_Box.3mf` y `Fam_Nufio_Aguilar_Box.3mf`,
generados con «Creador de LightBox» / MakerLab «Light Box V0.0.1» y guardados con Bambu Studio 2.3.2):

| Medida | Illescas (4:3) | Aguilar (apaisada) |
|---|---|---|
| Exterior de la caja | 174,664 × 132,036 mm | 174,664 × 96,902 mm |
| Imagen (interior de la pared) | 170,664 × 128,02 mm | 170,664 × 92,9 mm |
| Pared / fondo | 2 / 2 mm | 2 / 2 mm |
| Altura de la pared | 35 mm (37 con el fondo) | 20 mm (22 con el fondo) |
| Esquinas | exteriores de radio 2; interiores vivas | igual |
| Marco de la tapa | anillo de 2 mm de ancho y 0,8 mm de grueso | igual |
| Labio | anillo de 1 mm de ancho y 5 mm de alto; exterior 170,666 (= hueco, holgura 0) | igual |
| Imagen | 153 piezas de 0,8 mm de grueso (las manchas del SVG) | ~1000 piezas |
| Filamentos | PLA negro (marco y labio) y blanco (imagen) | igual |
| Impresión | Creality Hi, boquilla 0,4, capa 0,2 mm, 2 perímetros, 3 capas arriba y abajo, relleno 15 % | capa 0,16 mm, relleno 10 % |
| Placas | tapa y caja en dos placas aparte | igual |

La tapa se imprime cara abajo (la imagen contra la cama) y la caja con el fondo en la cama y la boca hacia arriba.
Aguilar lleva además un agujero cilíndrico de Ø26 mm que el usuario le añadió a mano (un «negative part» de Bambu
Studio); la caja de MakerLab no trae agujero para el cable.

## Bote con tapa

Un bote con tapa que se elige por su **forma**: la **redonda** cierra con **rosca** o con **bayoneta**, y la **rectangular**
con una **tapa deslizante**. La tapa sale aparte, al lado, con la cara de arriba sobre la cama. Una sola **holgura de
ajuste** (0,3 mm por cada lado por defecto) manda en los tres cierres; cámbiala según cómo imprima tu impresora.

- **Rosca:** el cuello es una varilla roscada (paso y alto a tu gusto; el paso grande imprime mejor) y la boca queda un poco
  más estrecha que el bote para que la pared del cuello no se quede fina. La tapa es un vaso con la rosca por dentro, más la holgura.
- **Bayoneta:** cuatro pines en el cuello (diámetro, cuánto salen y a qué altura). La tapa lleva una guía redonda a esa altura
  con cuatro entradas hasta el borde. Se alinean los pines con las entradas y se empuja hasta el fondo; para girar hay que
  **levantar la tapa un poco**, porque la guía va un escalón más arriba que las entradas (la **altura de la grada**, 1,5 mm
  por defecto, es lo que hay que levantar). Se gira hasta un **tope** (un muro junto a cada entrada, que impide seguir hasta
  la entrada siguiente) y, al soltar, los pines bajan a la **grada de cierre**: un escalón más hondo que la guía, del ancho
  justo de un pin, donde descansan. Para abrir hay que volver a levantar la tapa por encima del escalón y girar de vuelta;
  solo girando no se abre. El ajuste está comprobado con una prueba de volúmenes (`test_edge_ops.cpp`, «La tapa de bayoneta…»).
- **Deslizante:** guías en las paredes largas (hondo de la guía y labio que sujeta la tapa por arriba) y una ranura en la pared
  de delante para meter la tapa; la pared de atrás hace de tope.
- **Redondeo del fondo por dentro** (forma redonda): la unión de la pared con el fondo es un arco, no una esquina viva, que la
  refuerza donde más fuerza se aplica. El mismo campo está en la Caja con tapa redonda.

La tapa deslizante también cabe como cierre de la Caja con tapa y del Lightbox (las paredes ya existen; haría falta la ranura
de entrada y las guías): queda como mejora, ver «Pendiente de BOT» en la hoja de ruta.

## Caja con tapa

Una caja rectangular abierta por arriba y su tapa. La tapa cierra de una de dos maneras: con un **labio a presión** o
con **tornillos en las esquinas**. Sirve para electrónica, regalos u organizadores.

### Preguntas y medidas

| Pregunta | Parámetro | Por defecto | Mín. – máx. | Para qué sirve |
|---|---|---|---|---|
| Ancho (exterior) | `ancho` | 100 mm | 20 – 1000 | Largo de la planta en X |
| Fondo (exterior) | `fondo` | 60 mm | 20 – 1000 | Largo de la planta en Y |
| Alto de la caja | `alto` | 40 mm | 10 – 500 | Del fondo al borde, sin la tapa |
| Pared | `pared` | 2 mm | 0,8 – 20 | Grosor de las paredes |
| Base | `base` | 2 mm | 0,8 – 20 | Grosor del fondo |
| Radio de esquinas | `esquina` | 3 mm | 0 – 30 | Esquinas exteriores de la caja y de la tapa |
| Grosor de la tapa | `tapa` | 2 mm | 0,8 – 20 | Grosor de la placa de la tapa |
| Cierre | — | Labio a presión | Labio a presión / Tornillos en las esquinas | Cómo se cierra |
| Alto del labio | `labio` | 3 mm | 1 – 30 | Solo con labio: lo que entra la tapa en la caja |
| Grosor del labio | `labio_pared` | 1,2 mm | 0,4 – 10 | Solo con labio |
| Holgura del labio | `holgura` | 0,2 mm | 0 – 2 | Solo con labio: aire por cada lado |
| Diámetro de los postes | `poste` | 8 mm | 4 – 30 | Solo con tornillos: postes cilíndricos en las 4 esquinas, por dentro |
| Agujero del poste | `tornillo` | 2,5 mm | 1 – 10 | Solo con tornillos: para roscar el tornillo (M3: 2,5), de 10 mm de profundidad como mucho |
| Agujero de la tapa | `pasante` | 3,4 mm | 1 – 12 | Solo con tornillos: para que el tornillo pase (M3: 3,4) |

### Medidas derivadas (con los valores por defecto)

| Pieza | Medida |
|---|---|
| Caja | 100 × 60 × 40 mm, esquinas de radio 3 |
| Hueco | 96 × 56 mm (ancho − 2·pared, fondo − 2·pared) y 38 mm de profundidad |
| Tapa (labio) | placa de 100 × 60 × 2 mm y un anillo de 95,6 × 55,6 exterior (hueco − 2·holgura) y 93,2 × 53,2 interior, 3 mm de alto |
| Tapa (tornillos) | placa de 100 × 60 × 2 mm con cuatro agujeros de Ø3,4 |
| Postes | Ø8, centrados a `ancho/2 − pared − poste/2 + 0,5` en X y `fondo/2 − pared − poste/2 + 0,5` en Y de cada esquina (el 0,5 los mete en la pared para que se unan), desde la base hasta el borde |

### Operaciones que crea

Siempre: «Caja: cuerpo» (caja con esquinas redondeadas), «Caja: hueco» y «Tapa: placa» (al lado, en X = ancho + 10).
Con labio: «Tapa: labio» y «Tapa: hueco del labio». Con tornillos: «Caja: poste 1–4», «Caja: agujero del poste 1–4» y
«Tapa: agujero 1–4». Cuerpos **Caja** y **Tapa**, en el componente «Caja con tapa». Todas las medidas son
parámetros (`ancho`, `alto`…), así que se pueden cambiar después en Sólido › Parámetros.

## Letrero o llavero (con texto)

Una placa con tu texto, que puede ir **en relieve** (unido a la placa en **un solo cuerpo**, o en otro cuerpo aparte para imprimirlo en otro color), **grabado** en la
placa o como **letras sueltas**, y con una **anilla** opcional para convertirlo en llavero. Las letras salen del contorno
de la fuente elegida (las del sistema) y quedan en un sketch editable dentro del historial.

### Preguntas y medidas

| Pregunta | Parámetro | Por defecto | Mín. – máx. | Para qué sirve |
|---|---|---|---|---|
| Tipo | — | Letrero | Letrero / Llavero | Con el llavero se añade la anilla a la izquierda |
| Texto | — | Nufio | cualquier texto | Lo que se escribe |
| Fuente | — | la de la interfaz | las fuentes escalables del sistema | La forma de las letras |
| Tamaño del texto | — | 20 mm | 3 – 300 | Tamaño de la fuente (las mayúsculas miden ~70 %); no es un parámetro: cambia el sketch |
| Texto | — | En relieve (un solo cuerpo) | En relieve (otro cuerpo) / Grabado (hundido) / Solo letras / En relieve (un solo cuerpo) | Cómo va el texto. El orden de las opciones se mantiene (los diseños guardados guardan el índice): la nueva es la última y la de fábrica |
| Margen de la placa | — | 5 mm | 1 – 100 | Entre el texto y el borde; solo sirve para calcular el tamaño de la placa |
| Grosor de la placa | `grosor` | 3 mm | 0,8 – 50 | Grosor de la placa |
| Relieve o profundidad | `relieve` | 1,2 mm | 0,2 – 50 | Cuánto sobresale (o se hunde) el texto; con «Solo letras», su altura |
| Radio de esquinas | `esquina` | 3 mm | 0 – 50 | Esquinas redondeadas de la placa |
| Diámetro de la anilla | `anilla` | 12 mm | 5 – 60 | El disco que rodea el agujero (solo llavero) |
| Agujero de la anilla | `agujero` | 5 mm | 2 – 40 | Para pasar el aro del llavero (solo llavero) |

La placa mide el texto más dos márgenes: `placa_ancho` y `placa_alto` se calculan al crear el diseño y quedan como
parámetros (si cambias el texto después hay que editar el sketch y esas dos medidas).

### Operaciones que crea

| # | Nombre | Tipo | Medidas | Operación |
|---|---|---|---|---|
| 1 | «Letrero/Llavero: letras» | Sketch en XY | El contorno del texto, centrado en el origen y bloqueado | — |
| 2 | «…: placa» | Caja centrada, radio `esquina` | `placa_ancho` × `placa_alto` × `grosor` | Cuerpo nuevo |
| 3 | «Llavero: anilla» | Cilindro Ø `anilla`, a la izquierda de la placa (2 mm metidos en ella) | grosor de la placa | Unir |
| 4 | «Llavero: agujero» | Cilindro Ø `agujero` en el centro de la anilla | atraviesa la anilla | Cortar |
| 5 | «…: texto» | Extrusión de las letras | En relieve (otro cuerpo): de `grosor` a `grosor + relieve`, cuerpo nuevo. En relieve (un solo cuerpo): de `grosor − 0,01` a `grosor + relieve`, unido a la placa. Grabado: de `grosor − relieve` hacia arriba, cortando la placa. Solo letras: desde Z = 0, `relieve` de alto | Cuerpo nuevo / Unir / Cortar |

Cuerpos: **Placa** (relieve en un solo cuerpo, grabado; con la anilla en el llavero), **Placa** y **Texto** (relieve en otro cuerpo) o solo **Texto** (letras sueltas). Componente:
«Letrero» o «Llavero». Las regiones que rellenan el hueco de una letra (el interior de una «O», una «A»…) no se
extruyen.

## Par de engranajes

Dos ruedas que engranan, ya colocadas: mismo módulo y ángulo de presión, a la **distancia entre ejes** correcta y con
la segunda **girada** para que un diente de una caiga en el hueco de la otra. El formulario muestra la relación de
transmisión, la distancia entre ejes y los diámetros primitivos mientras cambias los valores.

### Cómo se calcula

| Qué | Fórmula |
|---|---|
| Diámetro primitivo | `módulo × dientes` |
| Relación de transmisión | `dientes 1 : dientes 2` (la rueda 2 gira `dientes 1 / dientes 2` veces por vuelta de la 1) |
| Distancia entre ejes | `módulo × (dientes 1 + dientes 2) / 2 + juego` |
| Giro de la rueda 2 | `(180° − 180°/dientes 2)` reducido a un paso (`360°/dientes 2`): con un diente de la rueda 1 apuntando a la 2, la 2 tiene un hueco apuntando a la 1 |
| Hélice | la rueda 1 con `+ángulo` y la rueda 2 con `−ángulo` (mismo ángulo, sentido contrario) |

### Preguntas y medidas

| Pregunta | Parámetro | Por defecto | Mín. – máx. | Para qué sirve |
|---|---|---|---|---|
| Módulo | `modulo` | 2 mm | 0,3 – 20 | Tamaño del diente, igual en las dos |
| Dientes de la rueda 1 | `dientes_1` | 12 | 6 – 300 | |
| Dientes de la rueda 2 | `dientes_2` | 24 | 6 – 300 | |
| Ángulo de presión | `presion` | 20° | 5 – 35 | Igual en las dos (20° es lo normal) |
| Ancho de las ruedas | `ancho_rueda` | 8 mm | 1 – 200 | A lo largo del eje |
| Ángulo de hélice | `helice` | 0° | 0 – 45 | 0 = dientes rectos |
| Agujero de la rueda 1 / 2 | `agujero_1`, `agujero_2` | 5 mm | 0 – 100 | 0 = macizo |
| Juego entre dientes | `juego` | 0,15 mm | 0 – 2 | Aire para que giren sin atascarse al imprimir |

Con los valores por defecto: ruedas de Ø24 y Ø48 mm, distancia entre ejes de 36,15 mm (36 + 0,15 de juego) y giro de la
rueda 2 de 7,5°. La rueda 1 queda en el origen y la 2 en X = distancia entre ejes.

### Operaciones que crea

| # | Nombre | Tipo | Operación |
|---|---|---|---|
| 1 | Engranaje: rueda 1 | Engranaje en el origen, con la hélice `+helice` | Cuerpo nuevo |
| 2 | Engranaje: rueda 2 | Engranaje en X = distancia entre ejes, girado `giro_rueda_2` (parámetro calculado), con la hélice `−helice` | Cuerpo nuevo |

Cuerpos **Rueda 1** y **Rueda 2** en el componente «Par de engranajes». Todo está ligado a parámetros: al cambiar los
dientes o el módulo en Sólido › Parámetros, la distancia y el giro se recalculan solos.

## Tren planetario

Un **sol** en el centro, **2 a 8 planetas** a su alrededor (3 a 120° por defecto) y una **corona** (engranaje interno) que
los rodea, con el **portasatélites** (una placa) y los **ejes** (uno por planeta y el central). Es el «spinner» que en
Fusion 360 se arma a mano, ya montado.

- **Condiciones de montaje:** la corona sale con `dientes del sol + 2 × dientes del planeta`; para que todos los
  planetas engranen a la vez, `(dientes del sol + dientes de la corona) ÷ planetas` debe ser entero (si no, el formulario
  avisa). Cada planeta lleva su **fase** calculada: `giro_planeta + θ·(zs + zp)/zp`, y la corona `giro_planeta·zp/zr`.
- **Hélice:** el sol lleva un sentido; los planetas y la corona, el contrario (como en el par y el engranaje interno).
- **Lo que trae puesto:** la **corona fija**; una **unión giratoria** en cada rueda y en el portasatélites; y un **enlace
  de movimiento** con la fórmula de Willis (corona quieta): por cada vuelta del sol, el portasatélites da `zs/(zs + zr)`
  vueltas (con sus ejes y planetas) y cada planeta gira sobre su eje `−zr·zs/((zs + zr)·zp)` veces. Sólido › Enlace de
  movimiento › «Animar» lo mueve; Sólido › Interferencias comprueba que nada se pisa a lo largo del giro.
- **Ajustes de impresión:** «Adelgazar el diente» sale en 0,1 mm (tres engranes a la vez necesitan aire para girar).

## Engranaje interno

Una **corona** con los dientes hacia dentro y un **piñón** que engrana en su interior, ya colocados. Es el mismo
engranaje de involuta de siempre con **Tipo = Interno (corona)**: lo que se dibuja es el hueco (con forma de rueda de
dientes) dentro de un aro. El primer hueco de la corona y el primer diente del piñón apuntan los dos por +X, así que
encajan sin girar nada.

| Qué | Fórmula |
|---|---|
| Diámetro primitivo | `módulo × dientes` (corona y piñón) |
| Relación | `dientes piñón : dientes corona`; el piñón gira en el **mismo sentido** que la corona |
| Distancia entre ejes | `módulo × (dientes corona − dientes piñón) / 2 − juego` (en un engranaje interno acercar los ejes separa los flancos) |
| Dientes de la corona | cabeza (punta) a `rp − módulo`, raíz (fondo del hueco) a `rp + 1,25·módulo` |
| Aro | disco exterior de radio `rp + 1,25·módulo + aro` |

| Pregunta | Parámetro | Por defecto | Mín. – máx. |
|---|---|---|---|
| Módulo | `modulo` | 2 mm | 0,3 – 20 |
| Dientes de la corona / del piñón | `dientes_corona`, `dientes_pinon` | 36 / 12 | 20 – 300 / 6 – 300 |
| Ángulo de presión | `presion` | 20° | 5 – 35 |
| Ancho | `ancho_rueda` | 8 mm | 1 – 200 |
| Grosor del aro de la corona | `aro` | 6 mm | 1 – 100 |
| Agujero del piñón | `agujero_pinon` | 5 mm | 0 – 100 |
| Juego entre dientes | `juego` | 0,15 mm | 0 – 2 |

Operaciones: «Engranaje: corona» (tipo Interno, en el origen) y «Engranaje: piñón» (en X = distancia entre ejes), cuerpos
**Corona** y **Piñón** en el componente «Engranaje interno». Solo dientes rectos (con hélice avisa). El formulario avisa
si la corona tiene menos de 8 dientes más que el piñón (los dientes se estorban; es el límite habitual de interferencia).

## Cremallera

Una **barra dentada** y la **rueda** que engrana con ella. La cremallera es una rueda de radio infinito: flancos rectos
con el ángulo de presión, diente de `2,25·módulo` de alto (cabeza `+1·m` y raíz `−1,25·m` sobre la línea primitiva).
Es el mismo engranaje con **Tipo = Cremallera** (los «Dientes» son los de la barra y «Aro o base» es el grosor bajo la raíz).

| Qué | Fórmula |
|---|---|
| Paso | `π × módulo` (con el modo por dientes, largo de la barra = `dientes × π × módulo`) |
| Dientes con el largo en mm | exacto: `ceil(largo / (π × módulo))` y la barra se corta a `largo`; con dientes completos: `max(2; round(largo / (π × módulo)))` |
| Avance por vuelta de la rueda | `π × módulo × dientes de la rueda` |
| Posición de la barra | línea primitiva a `Y = −(módulo × dientes rueda / 2) − juego` (tangente a la rueda, alejada el juego) |
| Giro de la rueda | `270°` reducido a un paso (`giro_rueda`, calculado): un diente de la rueda apunta hacia abajo, justo sobre un hueco de la barra |
| Posición X de la barra | `−floor(dientes barra / 2) × π × módulo`: deja un hueco de la barra bajo la rueda |

| Pregunta | Parámetro | Por defecto | Mín. – máx. |
|---|---|---|---|
| Módulo | `modulo` | 2 mm | 0,3 – 20 |
| Dientes de la rueda | `dientes_rueda` | 20 | 6 – 300 |
| **Largo de la barra** (elige cómo darlo) | `largo_modo` | Por número de dientes | por número de dientes · **largo exacto** (el último diente queda cortado) · **largo con dientes completos** (se ajusta al entero más cercano) |
| Dientes de la cremallera (solo en el modo por dientes) | `dientes_cremallera` | 10 | 2 – 300 |
| Largo exacto de la barra (solo en el modo exacto) | `largo_exacto` → parámetro `largo` | 60 mm | 8 – 2000 (máximo `300 × π × módulo`) |
| Largo aproximado (solo en el modo con dientes completos) | `largo_aprox` → parámetro `largo` | 60 mm | 8 – 2000 |
| Ángulo de presión | `presion` | 20° | 5 – 30 (la barra no admite más: el diente se solaparía en la raíz) |
| Ancho | `ancho_rueda` | 8 mm | 1 – 200 |
| Grosor de la base de la barra | `base` | 5 mm | 1 – 100 |
| Agujero de la rueda | `agujero_rueda` | 5 mm | 0 – 100 |
| Juego entre dientes | `juego` | 0,15 mm | 0 – 2 |

**Largo en milímetros.** Con el modo exacto, la barra mide justo lo pedido: se hacen los dientes que hagan falta y lo que
pasa del largo se **recorta en el propio perfil** (`GearSpec::length`), de modo que el último diente puede quedar
cortado y la cara de corte se llama `lado:extremo_d`; no hay ninguna booleana ni toca a la rueda. El largo queda
**ligado al parámetro `largo`**: se puede cambiar en Sólido › Parámetros y la barra se rehace (los dientes salen de la
expresión `ceil(largo/(pi*modulo))`). Los diseños hechos antes del modo de largo se reabren por dientes, como estaban.
El largo no puede pasar de `dientes × π × módulo` (se avisa) ni ser menor que un paso.

Operaciones: «Engranaje: rueda» y «Engranaje: cremallera» (tipo Cremallera); cuerpos **Rueda** y **Cremallera** en el
componente «Cremallera». Caras de la barra: `lado:d<i>:flanco_d|flanco_i|cabeza|raiz_d`, `lado:raiz_i`, `lado:base`,
`lado:extremo_i|extremo_d`.

## Primitivas: Caja y Cilindro

Son las piezas con las que se construyen los generadores; también se pueden usar solas (Sólido › Caja / Cilindro).

### Caja

| Campo | Parámetro (clave) | Significado |
|---|---|---|
| Plano o cara | — | Plano de origen (XY, XZ, YZ), plano de construcción o una cara plana elegida con un clic |
| Posición X, Y | `x`, `y` | Punto del plano (desde su origen o, en una cara, desde el origen de su plano) |
| Posición Z | `z` | Separación del plano a lo largo de la normal |
| Ancho | `width` | A lo largo de X del plano |
| Fondo | `depth` | A lo largo de Y del plano |
| Alto (±) | `height` | A lo largo de la normal; con signo negativo va hacia el otro lado |
| Centrada | — | El punto es el centro del ancho y del fondo (si no, es una esquina) |
| Alto repartido | — | El alto va mitad a cada lado del plano |
| Operación | — | Cuerpo nuevo, Unir, Cortar o Intersecar |

### Cilindro

Igual, con **Diámetro** (`diameter`) en lugar de ancho y fondo; el punto es siempre el centro de la base.

### Nombres de las caras

Cada cara se llama `f<id>:cara:<lado>`, donde `<id>` es el de la operación y `<lado>` es:

| Lado | Cara |
|---|---|
| `+x` / `−x` | La que mira hacia +X / −X del plano |
| `+y` / `−y` | La que mira hacia +Y / −Y del plano |
| `+z` / `−z` | La que mira hacia donde apunta la normal / al lado contrario |
| `lado` | La pared curva del cilindro |

Esos nombres se conservan al cortar, unir o cambiar medidas, así que una operación posterior que use una de esas
caras la sigue.

## Cómo agregar un generador

Un generador es una clase (`document::Generator`, en `src/document/src/Generators.cpp`) que dice sus preguntas
(`fields()`: longitudes y opciones, con campos que solo se muestran según otra opción) y construye una receta
(`make(valores, sufijo)`): parámetros con nombre, operaciones ligadas a ellos por expresiones, nombres de cuerpos y
un componente. Se registra en `generators()` y aparece solo en la cinta y en el menú. Las pruebas están en
`tests/test_edge_ops.cpp` (`[generator]`).

## Pendiente

Lo que queda de los generadores está en la hoja de ruta, «[Pendientes por categoría](hoja-de-ruta.md#pendientes-por-categoría)»
(categorías G — Generadores y E — Engranajes). Lo más cercano: poner tu SVG sobre la cara central de la Lightbox (relieve o
corte), avisar antes de perder lo editado a mano al reabrir un generador, y la Caja con tapa redonda con bisagra y clips.

## Tornillo sin fin y rueda

Un tornillo de 1 a 4 entradas y una rueda helicoidal que engrana con él, con los ejes cruzados a 90°. El **módulo** es el axial del tornillo y el transversal de la rueda; el **diámetro primitivo del tornillo** (de 8 a 12 módulos) fija el ángulo de avance (arctan(entradas × módulo / diámetro)), que es también el ángulo de hélice de la rueda. La distancia entre ejes es (Ø tornillo + Ø rueda)/2 más el juego, y la rueda sale girada para que un hueco caiga sobre un filete. Con un avance menor de 5° el tornillo suele no dejarse mover por la rueda (autobloqueo). La rueda es cilíndrica, sin garganta: el contacto es puntual. En la herramienta suelta es el tipo **Tornillo sin fin** del Engranaje.

## Engranajes cónicos

Un piñón (eje Z) y una corona (eje X) con los ejes a 90° y el vértice de los conos en común (a un radio primitivo de cada rueda del plano del extremo grande de la otra). El ángulo de cada cono sale de los dientes (tan = dientes de una / dientes de la otra). El **ancho de cara** se mide sobre el cono y no debe pasar de un tercio de la distancia al vértice. El diente es una aproximación (el perfil del extremo grande de una rueda recta, reducido hacia el vértice). En la herramienta suelta es el tipo **Cónico** del Engranaje, con «Dientes de la rueda pareja».

## Letrero con varias líneas

El texto del letrero es un cuadro de varias líneas (Intro empieza otra). Las líneas van una debajo de otra; la distancia de una línea base a la siguiente es el **tamaño del texto × 1,1 + «Espacio entre líneas»**. La **alineación** (centrado, a la izquierda, a la derecha) alinea las líneas entre sí; el conjunto queda centrado en la placa.

## Lightbox: canal para tira LED

Con «Canal para tira LED», un muro bajo (un anillo de «Grosor del muro del canal») se levanta desde el fondo a «Ancho de la tira» de las paredes: la tira se pega en el fondo, entre la pared y el muro. Avisa si la tira es tan ancha que no queda sitio.

## Caja con tapa: divisiones, patas, ranuras y tornillos

«Divisiones interiores» reparte la caja a lo ancho con paredes de su grosor (llegan 3 mm por debajo del borde); «Patas» añade cuatro discos bajo el fondo (la caja baja de Z = 0: el slicer la apoya sola); «Ranuras de ventilación» corta ranuras de 2 mm en el centro de la tapa; y «Tornillo» (M2 a M6) fija el poste, el agujero de roscar y el de paso de la tapa de ese tornillo.

## Caja con tapa: redonda, bisagra, ranuras en las paredes y clips

Todo es opcional. **Forma redonda:** un cilindro hueco con tapa redonda y los postes sobre la circunferencia (divisiones, bisagra, clips y ranuras de pared solo existen en la rectangular). **Bisagra:** tres nudillos a lo largo del borde de atrás (dos de la caja y uno de la tapa, con 0,4 mm de aire entre ellos) y un agujero para un pasador de filamento (el diámetro del pasador más 0,3 mm); la tapa sale **cerrada** sobre la caja, a 0,2 mm, y se imprime ahí mismo. **Ranuras de ventilación en las paredes:** N ranuras de 2 mm una sobre otra, a media altura, en las paredes largas, en las cortas o en las cuatro. **Clips:** en cada lado corto, un resalte de 1 mm en la caja y una lengüeta de la tapa con un gancho que queda justo bajo el resalte al cerrar (ancho y largo de la lengüeta ajustables); funciona con el cierre por labio o por tornillos.
