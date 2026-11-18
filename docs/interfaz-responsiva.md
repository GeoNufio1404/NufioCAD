# Interfaz responsiva — inventario (Fase 20, R1)

Qué se rompe cuando la pantalla es pequeña o tiene mucha escala. Es la lista de trabajo de R2 a R6 de la [hoja de ruta](hoja-de-ruta.md). Medido el **18 nov. 2026** en Windows (Release), con una pantalla física de 1920×1080.

## Cómo se mide

El programa trae un modo que abre la ventana con un tamaño y una escala dados, recorre cada pestaña de la cinta, cada herramienta con panel, cada generador, los cuatro diálogos y el sketch, guarda una captura de cada uno y escribe `inventario.txt`:

```bash
NufioCAD --inventario capturas-1280x720 --tamano 1280x720 --escala 1
```

- `--tamano AnchoxAlto`: tamaño inicial de la ventana (en píxeles lógicos).
- `--escala N`: factor de escala de pantalla (equivale a `QT_SCALE_FACTOR`). **Emula** la escala sobre la pantalla real: con una pantalla de 1920×1080, escala 1,5 se comporta como una de 1280×720 y escala 2 como una de 960×540. Con escalas menores que 1 se emulan pantallas más grandes (0,75 → 2560×1440; 0,5 → 3840×2160).
- El informe anota por cada escena: el tamaño mínimo que pide la ventana frente a la pantalla disponible, los widgets recortados por su contenedor o fuera de la pantalla, los textos cortados, y si el panel de herramienta cabe o se puede desplazar.

Límites del modo: no ve lo que pasa con varios monitores de escalas distintas, ni si un texto es legible a ojo (para eso están las capturas), y los widgets que dibujan su propio texto (como los del visor) no se miden.

## Resultados por pantalla

Pantalla disponible = lo que queda sin la barra de tareas.

| Pantalla lógica | Cómo se emuló | Escenas que no caben | Paneles cortados a lo ancho |
|---|---|---|---|
| 3840×2160 | escala 0,5 | 0 | 3 |
| 2560×1440 | escala 0,75 | 0 | 3 |
| 1920×1080 | escala 1 | 0 | 3 |
| 1366×768 | ventana de 1366×768, escala 1 | 0 (pero la ventana crece, ver abajo) | 3 |
| 1536×864 | escala 1,25 | **5** de 22 | 3 |
| 1280×720 | escala 1,5 | **10** de 22 | 3 |
| 960×540 | escala 2 | **17** de 22 | 3 |

«No caben» = con esa escena abierta, el tamaño mínimo de la ventana es mayor que la pantalla disponible, así que la ventana crece y parte queda fuera.

## Qué se rompe y por qué

**1. La ventana crece según el panel de herramienta que se abra (lo que deja la interfaz inutilizable).**
Sin herramienta abierta la ventana pide como mínimo 566×544 y cabe en casi todo. Al abrir una herramienta, el panel «Propiedades» no tiene desplazamiento: su contenido manda y empuja el tamaño mínimo de toda la ventana. Con la ventana en su pantalla de 1366×768 se ve crecer a 927 de alto al abrir el Engranaje.

| Escena | Mínimo que pide la ventana |
|---|---|
| Barrido, Loft, Eje | 927×544 |
| Plano de construcción | 927×567 |
| Cilindro | 927×694 |
| Caja | 927×788 |
| Rosca | 927×724 |
| Engranaje | **927×927** |
| Generador Lightbox | 927×894 |
| Generador Caja con tapa | 927×831 |
| Generador Letrero | 927×756 |
| Generador Par de engranajes | 927×850 |
| Generador Engranaje interno | 927×817 |
| Generador Cremallera | **1019×847** |

Dos causas, separadas: (a) el ancho mínimo salta a 927 porque el panel de herramienta tiene un ancho mínimo (~350) y los acoplables (navegador y propiedades) más el visor suman eso; (b) el alto mínimo es el del contenido del panel más la cinta, la línea de tiempo y la barra de estado.
Cualquier pantalla de menos de ~930 px de alto (todas las de 720 y 768, y las de 864 y 1080 con escala ≥ 1,25) no puede mostrar el Engranaje o los generadores grandes enteros.

**2. Los generadores de engranajes se cortan por los lados.**
El Par de engranajes pide un panel de 497 px de ancho y el panel mide 349; el Engranaje interno pide 464 frente a 349 y la Cremallera 497 frente a 441. Ocurre en todas las pantallas, incluida la de 4K, porque el ancho del panel es fijo. Es el único problema que se ve también en la pantalla de uso diario.

**3. Sin desplazamiento en ningún panel.**
Ninguno de los 14 paneles de herramienta que se abrieron tiene barra de desplazamiento (el informe marca «SIN desplazamiento» en todos). Es la causa común de 1 y 2.

**4. La cinta solo se desplaza.**
Cuando no caben los botones aparece una barra horizontal debajo de la cinta (en la captura a escala 1,5 el grupo «Generadores» queda cortado: «GENE…»). Hay que ir arrastrando la barra para llegar a Engranaje, Rosca, Medir y Sección. No hay forma de recoger grupos en menús.

**5. Con escala de 200 % ni la ventana vacía cabe.**
La ventana sin nada abierto pide 566×544 y la pantalla disponible es 960×516: el alto ya no cabe, la barra de estado y la línea de tiempo quedan fuera de la pantalla.

**6. Los diálogos sí caben.** Exportar (380×401), Parámetros (560×360), Configuración (252×146) y Cuerpos (560×340) quedan dentro de la pantalla en todas las configuraciones. Faltan por revisar los de guardar/abrir del sistema, el aviso de recuperación y los diálogos que se abren con una selección (no entran en este inventario).

**7. Escala y DPI (a revisar en R5).** El modo solo emula la escala con `QT_SCALE_FACTOR`; falta probar una pantalla real con 125 % o 150 % y el paso de una ventana entre dos monitores con escalas distintas.

## Estado final de la Fase 20 (18 nov. 2026)

Con el mismo inventario, tras R2 a R6 (paneles con desplazamiento, cinta adaptable, paneles que se pliegan, escala de OpenCASCADE y diálogos acotados): **0 problemas en las 9 pantallas** y salida 0 del modo `--inventario`.

| Pantalla lógica | Antes (R1) | Después |
|---|---|---|
| 3840×2160 (escala 0,5) | 0 escenas no caben, 3 paneles cortados | 0 y 0 |
| 2560×1440 (0,75) | 0 y 3 | 0 y 0 |
| 1920×1080 (1) | 0 y 3 | 0 y 0 |
| 1366×768 (1) | 0 y 3 (la ventana crecía a 927 de alto) | 0 y 0 |
| 1536×864 (1,25) | 5 y 3 | 0 y 0 |
| 1280×720 (1,5) | 10 y 3 | 0 y 0 |
| 960×540 (2) | 17 y 3 | 0 y 0 |
| 1024×768 (1), 1280×720 (1) | (no medidas) | 0 y 0 |

Qué cambió, por problema de arriba:
1. **La ventana crecía con el panel** → el panel va en un área con desplazamiento y no pide tamaño a la ventana (R2). Con una herramienta abierta el mínimo pasó de 927×927 a 872×544 y, con navegador y línea de tiempo plegados, la ventana cabe en 960×516.
2. **Generadores cortados por los lados** → el panel se abre del ancho que pide el contenido (hasta el 40 % de la ventana) y, si no cabe, se desplaza (R2).
3. **Sin desplazamiento** → resuelto; Aceptar y Cancelar quedan fijos abajo.
4. **Cinta solo con desplazamiento** → se quitan los nombres, luego los últimos grupos pasan al botón «Más »» (R3).
5. **Con 200 % ni la ventana vacía cabía** → el navegador y la línea de tiempo se pliegan solos y la cinta se compacta cuando la ventana es angosta o baja (R4).
6. **Escala y DPI** → los textos y el cubo de vistas de OpenCASCADE siguen la escala (R5).

## Qué falta medir

- El visor con el sketch a escalas altas (iconos y cotas con tamaño en píxeles fijos).
- La ventana en Linux (Fedora) con las escalas de GNOME.
- Las pestañas de la cinta que solo existen con un sketch o una selección.

## Orden de trabajo (R2 a R6)

1. **R2 — Paneles con desplazamiento:** meter el contenido del panel «Propiedades» en un área con barra de desplazamiento y quitarle el efecto sobre el tamaño mínimo de la ventana. Con esto desaparecen los puntos 1, 2 y 3 en un solo cambio.
2. **R5 — Ancho del panel proporcional** a la ventana, y tamaños en unidades lógicas.
3. **R3 — Cinta con grupos que se recogen en menús** (punto 4).
4. **R4 — Navegador y línea de tiempo plegables**, plegados por defecto en pantallas chicas (punto 5).
5. **R6 — Convertir este inventario en una prueba** que falle si una escena no cabe, y correrla en el CI.
