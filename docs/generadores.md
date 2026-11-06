# Herramientas generadoras: medidas y convenciones

Qué produce cada generador (pestaña **HERRAMIENTAS** o menú **Herramientas**), con todas sus medidas, los nombres que
le pone a las cosas y cómo se relaciona cada parámetro con la geometría. El estado de cada generador y lo que falta
está en [`hoja-de-ruta-cad.md`](hoja-de-ruta-cad.md) (Fase 10) y cómo probarlos, en
[`guia-de-pruebas.md`](guia-de-pruebas.md).

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
| Ancho | a lo largo de X |
| Alto | a lo largo de Y |
| Profundidad | a lo largo de Z: el **frente** está en Z = 0 y el fondo hacia +Z; la caja se abre por **detrás** (en Z = profundidad) |
| Tapa | sale **al lado** de la caja, en +X a `ancho + 10` mm del centro, apoyada en Z = 0, lista para imprimir |
| Cuerpos | «Caja» y «Tapa», en el componente «Lightbox» |

El frente se imprime apoyado en la cama (cara en contacto con la cama = Z = 0) y la tapa también.

## Lightbox (rectangular y redonda)

Caja de luz: un frente fino que difunde la luz, paredes, una tapa trasera con un labio que entra en la caja y un
agujero para el cable de la tira LED.

### Preguntas y medidas

| Pregunta | Parámetro | Por defecto | Mín. – máx. | Para qué sirve |
|---|---|---|---|---|
| Forma | — | Rectangular | Rectangular / Redonda | Caja de planta rectangular o cilíndrica |
| Ancho | `ancho` | 150 mm | 20 – 1000 | Largo de la caja en X (solo rectangular) |
| Alto | `alto` | 100 mm | 20 – 1000 | Largo de la caja en Y (solo rectangular) |
| Diámetro | `diametro` | 120 mm | 20 – 1000 | Diámetro exterior (solo redonda) |
| Profundidad | `profundidad` | 40 mm | 10 – 500 | Grosor total de la caja, de adelante hacia atrás |
| Pared | `pared` | 2,4 mm | 0,8 – 20 | Grosor de las paredes laterales |
| Frente | `frente` | 1,2 mm | 0,4 – 10 | Grosor del panel frontal que difunde la luz: más fino, más luz pasa |
| Tapa trasera | `tapa` | 2,4 mm | 0,8 – 20 | Grosor de la placa de la tapa |
| Labio de la tapa | `labio` | 3 mm | 1 – 30 | Cuánto entra la tapa dentro de la caja |
| Holgura de la tapa | `holgura` | 0,2 mm | 0 – 2 | Aire entre el labio y la pared, por cada lado, para que encaje sin forzar |
| Agujero del cable | `cable` | 6 mm | 0 – 50 | Diámetro del agujero; 0 = sin agujero |
| Salida del cable | — | Atrás (en la tapa) | ver abajo | Por dónde sale el cable |

Recomendaciones para una boquilla de 0,4 mm: la pared y la tapa como múltiplos del ancho de línea (2,4 mm = 6
líneas); el frente de 1,2 mm (3 líneas) deja pasar bastante luz con filamento claro; una holgura de 0,2 mm es un
buen punto de partida (subirla a 0,3 si la tapa queda dura).

### Salida del cable

| Opción | Dónde hace el agujero |
|---|---|
| Atrás (en la tapa) — por defecto | Atraviesa la placa y el labio de la tapa, centrado en X y a un cuarto del alto por debajo del centro |
| Pared de abajo | Atraviesa la pared inferior (Y negativo), centrado en X y a media altura del hueco |
| Pared de arriba | Igual, por la pared superior (Y positivo) |
| Pared izquierda | Atraviesa la pared de X negativo, centrada en Y y a media altura del hueco |
| Pared derecha | Igual, por la pared de X positivo |

A media altura del hueco quiere decir en Z = `frente + (profundidad − frente) / 2`. En la lightbox redonda las cuatro
paredes son la misma pared curva (abajo, arriba, izquierda y derecha son direcciones del círculo).

### Medidas derivadas (con los valores por defecto)

| Pieza | Medida |
|---|---|
| Caja, exterior | 150 × 100 × 40 mm |
| Hueco | 145,2 × 95,2 mm de planta (ancho − 2·pared, alto − 2·pared) y 38,8 mm de profundidad (profundidad − frente) |
| Placa de la tapa | 150 × 100 × 2,4 mm |
| Labio de la tapa | 144,8 × 94,8 mm de planta (hueco − 2·holgura) y 3 mm de altura |
| Tapa, alto total | 5,4 mm (tapa + labio) |
| Volumen de la caja sin agujero | ≈ 150·100·40 − 145,2·95,2·38,8 ≈ 63 700 mm³ |

La versión redonda usa las mismas fórmulas con círculos: diámetro exterior, hueco de diámetro − 2·pared, labio de
diámetro − 2·pared − 2·holgura.

### Operaciones que crea (rectangular)

| # | Nombre | Tipo | Medidas (expresiones) | Operación |
|---|---|---|---|---|
| 1 | Lightbox: caja | Caja centrada | ancho × alto × profundidad | Cuerpo nuevo |
| 2 | Lightbox: hueco | Caja centrada, desplazada `frente` en Z | (ancho − 2·pared) × (alto − 2·pared) × profundidad | Cortar |
| 3 | Lightbox: agujero del cable | Cilindro Ø `cable` por la pared elegida (no si es «Atrás») | longitud `pared + 2`, repartida a los dos lados de la pared | Cortar |
| 4 | Lightbox: tapa | Caja centrada en X = ancho + 10 | ancho × alto × tapa | Cuerpo nuevo |
| 5 | Lightbox: labio de la tapa | Caja centrada en X = ancho + 10, desde Z = 0 | (ancho − 2·pared − 2·holgura) × (alto − 2·pared − 2·holgura) × (tapa + labio) | Unir |
| 6 | Lightbox: agujero del cable (tapa) | Cilindro Ø `cable` desde Z = −1, en Y = −alto/4 (solo con «Atrás») | longitud `tapa + labio + 2` | Cortar |

En la redonda, la caja, el hueco, la tapa y el labio son cilindros (el diámetro hace de ancho y de alto).

### Nombres

- Cuerpos: **Caja** y **Tapa**; componente **Lightbox**.
- Operaciones: las de la tabla («Lightbox: …»).
- Caras: cada caja y cada cilindro nombra sus caras por su orientación (ver más abajo), así que se puede, por
  ejemplo, dibujar un sketch en la cara frontal de la caja o aplicar un redondeo a las aristas, y la operación sigue
  su cara si cambias una medida.

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

Ver la hoja de ruta. Lo más cercano: el canal para la tira LED, la tapa en el frente con una **cara central**
separada de los bordes (para pintarla aparte), y los demás generadores (caja con tapa, letreros, par de engranajes).
