# Hoja de ruta — NufioCAD (CAD paramétrico de escritorio en C++)

## Visión

Un CAD paramétrico de escritorio para **Windows y Linux**, **offline y con archivos locales privados**, con la facilidad de uso de **Fusion 360** como meta. El flujo principal: hacer un sketch 2D sobre un plano o una cara, extruirlo y construir la pieza en 3D con un historial editable, para luego exportarla a STL, OBJ o STEP e imprimirla en 3D.

**Motivación:**
- FreeCAD: mismo tipo de kernel, pero su UX no resulta intuitiva (workbenches separados, flujo confuso).
- Onshape: requiere internet y, en el plan gratuito, los modelos son públicos.
- Fusion 360: es la referencia de UX a alcanzar.

---

## Stack tecnológico

| Capa | Tecnología |
|---|---|
| Lenguaje | C++ |
| Kernel geométrico (B-Rep) | OpenCASCADE (OCCT) |
| Interfaz | Qt 6 Widgets (Dear ImGui solo para prototipos) |
| Archivo nativo | `.nuf` sobre SQLite (parámetros en JSON) |
| Render 3D | Visor AIS de OCCT al inicio; OpenGL propio más adelante si hace falta |
| Solver de restricciones 2D | `libslvs` (SolveSpace) o PlaneGCS (FreeCAD) |
| Plataformas | Windows y Linux, compilación nativa (sin Docker para distribuir) |
| Build | CMake + CMakePresets + vcpkg (manifiesto); CI en Windows y Linux |

**Capacidades de OCCT que aprovechamos:** extrusión, revolución, sweep, loft, booleanas, chaflán, fillet, shell; exportación STEP (`STEPControl_Writer`); mallado para STL (`BRepMesh_IncrementalMesh` + `StlAPI_Writer`).

---

## Principios de UX (referencia: Fusion 360)

1. **Timeline** abajo, siempre visible: arrastrar, editar o suprimir operaciones.
2. **Un solo espacio de trabajo**, sin workbenches separados.
3. **Manipuladores en pantalla**: flechas para extruir, valores editables junto al cursor.
4. **Sketch con restricciones automáticas** (horizontal, tangente, coincidente) mientras se dibuja.
5. **Menú radial** con clic derecho y **atajos de una tecla** (E = extruir, L = línea).
6. **Vista previa en vivo** antes de confirmar cada operación.

---

## Concepto central: modelado paramétrico

- El modelo es un **árbol/historial de operaciones** (Sketch1 → Extrude1 → Fillet1 → …).
- Cada operación guarda sus **parámetros**, no su resultado; al editar una, se reconstruye todo lo que viene después.
- **Riesgo principal: problema de nombrado topológico.** Las referencias a caras y aristas (ej. "arista 7") pueden cambiar al editar operaciones anteriores. Debe resolverse en la Fase 3 (OCCT ofrece OCAF/TNaming como apoyo); si se pospone, chaflanes y fillets romperán el historial.

---

## Formatos de archivo (decisión)

| Uso | Formato | Notas |
|---|---|---|
| Nativo (guardar/abrir) | **`.nuf`** | Formato propio sobre SQLite: local, privado, guarda el historial completo, guardado atómico |
| Exportar | STL, OBJ, STEP | STL/OBJ como malla para el slicer; STEP como sólido exacto |
| Importar | STEP | Entra como sólido sin historial (una operación «Importación» en el timeline), igual que en Fusion 360 |

**Descartado: `.f3d` de Fusion 360.** Es un formato propietario de Autodesk sin documentación pública; su geometría usa el formato del kernel de Autodesk (ShapeManager), que OCCT no lee, y reconstruir su historial implicaría replicar el comportamiento interno de Fusion. Solo lo abre Fusion, así que no aporta como estándar de intercambio.

---

## Fases

### Fase 0 — Diseño y fundamentos
- Principios de UX documentados.
- Mockups de la interfaz: modelado, sketch, inicio, importar y exportar.
- Arquitectura: kernel (OCCT) / modelo del documento (features) / sistema de comandos (deshacer/rehacer) / UI (Qt).
- Entorno: CMake + Qt + OCCT, estructura de carpetas, repositorio Git.
- Nombre de trabajo: **NufioCAD** (puede cambiar); extensión de archivo `.nuf`; prefijo `nuf` en el código.

### Fase 1 — Visor y navegación
- Viewport 3D.
- Órbita, pan y zoom estilo Fusion.
- ViewCube.
- Selección de caras y aristas con resaltado al pasar el cursor.
- Barra de navegación flotante sobre el visor (inicio, ajustar, mirar al sketch, y herramientas Orbitar, Desplazar y Zoom).

### Interfaz estilo Fusion 360 (transversal)

Hecho (probado en Windows; en Linux falta compilar con `qt6-qtsvg`):
- [x] Iconos SVG (Lucide, ISC) con nombre y tooltip en todas las acciones; se recolorean con el tema claro/oscuro en caliente.
- [x] Cinta (*ribbon*) con pestañas **Sólido** y **Sketch** (contextual, aparece al editar un sketch), grupos con título, desplegables por familia que recuerdan la última variante y botón verde «Terminar sketch».
- [x] Barra de navegación inferior flotante.
- [x] Flecha de extrusión: ya no se suelta tras el primer paso y el arrastre va fluido (un valor por cuadro, la flecha se reutiliza).
- [x] Como Fusion 360 (revisado contra su documentación): la aplicación abre **directo en un lienzo vacío**, sin pantalla de inicio; los recientes con miniatura están en Archivo › Recientes… (Ctrl+Mayús+O, se cierra con Esc) y en Abrir reciente. El panel de propiedades ya no está fijo y vacío: aparece solo mientras hay un comando activo, como los diálogos de Fusion.
- [x] Medidas visibles en el sketch: largo de cada línea y Ø/R de círculos y arcos, sin tener que acotar (lo que ya tiene cota propia, lo proyectado y la construcción no se rotulan). Se apagan en Ver › Mostrar medidas del sketch.
- [x] Sketch sin saturar (referencias: Fusion 360 para el flujo y la jerarquía, Onshape para mostrar lo secundario solo donde se mira): las **medidas y las restricciones solo se ven en lo que tiene el cursor encima o está seleccionado**; Ver › «Medidas/Restricciones de todo el sketch a la vez» las muestra todas. Tres niveles visuales: las cotas que pone el usuario, en azul y con sus líneas; las restricciones, como iconos pequeños y apagados; y las medidas automáticas, como texto simple sin recuadro.
- [ ] Paleta flotante del sketch sobre el lienzo (casillas: medidas, restricciones, puntos, rejilla), como la de Fusion; evitar que los textos se tapen entre sí.
- [ ] Barra de aplicación superior (Guardar, Deshacer, Rehacer y pestaña del documento) y selector de espacio de trabajo, como en Fusion; el navegador con el nombre del documento como raíz; controles de reproducción en el timeline; los comandos como diálogo flotante sobre el lienzo en vez de un panel acoplado.
- [ ] Menú radial con clic derecho y atajos de una tecla para todo (hoy hay atajos de sketch, E y Mayús+P).
- [ ] Valores editables junto al cursor también en Extruir/Revolución (hoy solo en el sketch y en el panel).
- [ ] Rendimiento de la vista previa medido en Release (en Debug quedan ~25 ms por paso; Smart App Control de esta PC bloquea las DLL de OCCT en Release).

### Fase 2 — Sketcher 2D

Las variantes de creación (rectángulo por centro, círculo tangente, polígono
circunscrito…) no son figuras nuevas: se descomponen en unas pocas entidades
(punto, línea, círculo, arco, elipse, spline) más restricciones que resuelve el
solver. Por eso la fase se divide así:

**2a — Núcleo** (lo mínimo para que la Fase 3 pueda extruir)
- Solver de restricciones (PlaneGCS) detrás de `ISolver`.
- Modelo del sketch: entidades, restricciones y cotas, sin Qt.
- Modo sketch sobre un plano de origen o una cara plana; la cámara se orienta al plano.
- Rejilla y ajuste (*snap*) a puntos, extremos, puntos medios, centros e intersecciones.
- Punto, línea (polilínea continua), rectángulo por 2 esquinas, círculo por centro y radio, arco por 3 puntos.
- Restricciones: coincidente, horizontal, vertical, paralela, perpendicular, tangente, igual, colineal, concéntrica, punto medio, simétrica y fija.
- Restricciones automáticas al dibujar, con indicador junto al cursor (y tecla para no aplicarlas).
- Cotas editables: lineal, alineada, horizontal/vertical, angular, radio y diámetro; cotas de referencia (no restringen). Valores introducidos junto al cursor mientras se dibuja.
- Estado de la restricción: color según esté sin restringir / totalmente restringido, grados de libertad restantes y aviso de restricciones redundantes o en conflicto.
- Arrastrar geometría no restringida (el solver la mueve respetando las restricciones).
- Geometría de construcción (alternar con una tecla).
- Detección de perfiles cerrados y regiones (incluidas islas), sombreadas en pantalla.
- Deshacer/rehacer dentro del sketch.

**2b — Variantes de creación**
- Línea con arco tangente al arrastrar desde el extremo (como Fusion).
- Rectángulo por 3 puntos (girado) y por centro.
- Círculo por 2 puntos, por 3 puntos, tangente a 2 entidades y tangente a 3 entidades.
- Arco por centro, arco tangente y arco por 3 puntos.
- Polígono inscrito, circunscrito y por lado.
- Ranura (*slot*): centro a centro, total, por punto central y en arco.
- Elipse y arco de elipse.
- Spline por puntos de paso y por puntos de control; restricción de curvatura (G2).
- Curva cónica.

**2c — Edición del sketch**
- Recortar, extender y partir (*break*).
- Empalme y chaflán 2D.
- Desfase (*offset*).
- Simetría (espejo) respecto a una línea.
- Patrones rectangular y circular dentro del sketch.
- Mover, copiar, rotar y escalar selecciones.
- Proyectar aristas y caras del modelo al sketch (con vínculo al original) e intersección del modelo con el plano del sketch.

**Pruebas manuales pendientes (2a y 2b)**

Abrir con `./build/linux-debug/src/app/NufioCAD` y comprobar:

1. **Crear sketch** → plano XY. La cámara mira el plano de frente y aparece la rejilla.
2. Familias con desplegable (Rectángulo, Círculo, Arco, Polígono, Ranura, Elipse, Spline): el botón recuerda la última variante usada.
3. **Polígono** (P): clic en el centro, mover, escribir `8`, Tab, `30`, Enter → octógono de 30 mm.
4. **Ranura en arco**: tres clics para el arco central y uno para el ancho.
5. **Elipse**: centro, extremo del eje mayor y ancho.
6. **Spline por puntos de paso**: varios clics y Enter; luego, sin herramienta, arrastrar uno de sus puntos.
7. **Arco tangente arrastrando**: L, dibujar una línea y arrastrar desde su extremo; la cadena sigue.
8. **Curvatura (G2)**: con una línea y una spline unidas, seleccionar los dos extremos y aplicar G2.
9. **Cónica**: inicio, fin; escribir `0.3` (rho) y clic para el vértice.
10. Rectángulo desde el origen con `40` Tab `25` Enter → queda en blanco (totalmente restringido); doble clic en una cota para cambiarla; Ctrl+Z / Ctrl+Y; Terminar sketch → queda visible en el modelo.

**Pruebas manuales pendientes (2c y 2d)**

11. **Recortar** (T): una línea cruzada por otras dos; clic en un tramo → desaparece y los extremos quedan sobre las que cortaban. **Extender** y **Partir** desde el mismo desplegable.
12. **Empalme**: escribir `5`, clic en la esquina de un rectángulo. **Chaflán** igual.
13. **Desfase** (O): clic en un lado de un rectángulo (se desfasa entero), mover el cursor dentro/fuera y clic (o escribir la distancia).
14. **Simetría**: seleccionar figuras (Shift+arrastrar = rectángulo de selección), Simetría y clic en el eje Y.
15. **Mover / Copiar / Girar / Escalar** y **Patrón rectangular / circular** sobre la selección (los valores se escriben junto al cursor; Enter).
16. **Proyectar aristas** (Mayús+P) en un sketch sobre XY: clic en el borde de la torre → círculo violeta (bloqueado; K lo desbloquea). **Intersección con la pieza** en un sketch sobre XZ.
17. **Texto**: clic, escribir el texto y la altura → letras violetas que forman perfiles. **Imagen de referencia**: clic, elegir un PNG/JPG y el ancho.
18. **Importar DXF…** (menú Sketch) con un dibujo de otro programa.
19. **Sketch terminado en la vista 3D**: tras Terminar sketch, clic dentro de una región (p. ej. un círculo dentro de un rectángulo) → se selecciona ese perfil («1 perfil de Sketch 1»); clic en una línea → esa curva. Doble clic → se reabre para editarlo.

**2d — Extras**
- Texto (con fuentes del sistema), para grabados y relieves.
- Imagen de referencia (*canvas*) para calcar.
- Importar DXF a un sketch.

### Fase 3 — Modelado básico + timeline

El modelo pasa a ser un historial de operaciones que se recalcula: el
documento guarda los parámetros (sketch, distancia, perfiles elegidos…) y la
pieza es solo el resultado. Se divide así:

**3a — Núcleo paramétrico** (sin interfaz, con pruebas)
- `Feature`, `Timeline` con marcador (lo que queda después no se calcula) y recálculo desde la operación editada, reutilizando el resultado en caché de la anterior.
- Cuerpos del modelo envueltos en `geometry/` (el resto del código no ve OCCT).
- Operación **Sketch**: plano de origen o cara de un cuerpo (sigue a la cara si la pieza cambia).
- Operación **Extruir**: uno o varios perfiles; extensión por distancia, simétrica, dos lados, hasta una cara y «a través de todo»; ángulo de inclinación (*taper*); desfase de inicio; resultado unir, cortar, intersecar o cuerpo nuevo (con elección automática como en Fusion).
- Operación **Revolución**: eje = línea del sketch, eje de origen o arista recta; ángulo, completa, simétrica o dos lados; mismos resultados que extruir.
- **Nombrado topológico** con un resolvedor propio (decisión de la sección 6 de la arquitectura): cada cara y arista recibe un nombre que describe su origen (operación + curva del sketch + papel: lateral, tapa inicial, tapa final) y se propaga por las booleanas con el historial de OCCT. Las referencias se guardan como nombres, nunca como «cara 7».
- Errores sin perder datos: si una operación falla queda marcada y las siguientes en advertencia.
- Comandos con memento (copia de la operación antes/después): agregar, editar, borrar, suprimir, reordenar y mover el marcador.

**3b — Interfaz del modelado**
- Los sketches pasan a ser operaciones del documento (hoy se guardan en la ventana); editar un sketch es un solo paso de deshacer.
- **Navegador** (izquierda): origen (planos y ejes), cuerpos y sketches, con mostrar/ocultar.
- **Timeline** (abajo): un icono por operación en orden; doble clic para editar, clic derecho para suprimir/borrar.
- Herramienta **Extruir** (E): elegir perfiles (ya seleccionables en la vista 3D) o una cara plana de un cuerpo; panel de propiedades a la derecha, flecha en pantalla para arrastrar la distancia, valor junto al cursor y vista previa en vivo (naranja para cortar). Aceptar/Cancelar.
- Herramienta **Revolución**: perfil + eje (de origen, línea del sketch o arista recta), con ángulo, vuelta completa, simétrica o dos lados.
- Deshacer/rehacer de todo el modelado (Ctrl+Z / Ctrl+Y).
- La pieza de demostración desaparece: se empieza con el documento vacío.

**3c — Timeline editable y reconstrucción**
- Editar cualquier operación pasada: se recalcula lo que sigue y las referencias (caras de sketches, «hasta cara») se resuelven por nombre.
- Arrastrar operaciones para reordenarlas (si las dependencias lo permiten) y arrastrar el marcador para volver atrás en el historial (*rollback*).
- Suprimir/reactivar y borrar (avisando si otras dependen de ella).
- Lo proyectado en un sketch queda vinculado a la arista original y se actualiza con la pieza.
- Iconos de error/advertencia en el timeline y el navegador, con el motivo.

**Pruebas manuales pendientes (3a y 3b)**

20. **Extruir**: sketch en XY con un rectángulo → Terminar sketch → clic dentro del rectángulo → **E**. A la derecha aparece el panel; arrastrar la flecha naranja o escribir la distancia; Enter o Aceptar. Aparece «Cuerpo1» en el navegador y «Extrusión1» en el timeline; el sketch se oculta solo.
21. **Sketch en una cara**: Crear sketch → clic en la cara superior → círculo → Terminar. Clic dentro del círculo → E → distancia negativa: la operación cambia sola a **Cortar** (vista previa del agujero) → Aceptar.
22. **Editar**: doble clic en «Extrusión1» del timeline → el modelo vuelve a ese punto → cambiar la distancia → Aceptar: el agujero sube o baja con la cara.
23. **Deshacer/Rehacer** (Ctrl+Z / Ctrl+Y) de cada paso; editar un sketch completo es un solo paso.
24. **Variantes de extruir**: Simétrica, Dos lados, A través de todo, Hasta una cara (botón «Elegir cara de destino» y clic en la cara), Ángulo y Desfase inicial. Unir dos cuerpos separados con un puente.
25. **Empujar una cara**: sin sketch, clic en una cara plana de un cuerpo → E → distancia.
26. **Revolución**: sketch en XZ con un rectángulo a la derecha del eje → clic en él → Sólido › Revolución → Eje Z (o clic en una línea del sketch) → Vuelta completa.
27. **Timeline**: arrastrar el marcador naranja hacia atrás y adelante; clic derecho en una ficha → Suprimir / Reactivar / Renombrar / Borrar (avisa si otras dependen de ella) / Mover el marcador aquí; arrastrar una ficha a otro sitio (no deja poner una extrusión antes de su sketch).
28. **Navegador**: casillas para mostrar/ocultar cuerpos, sketches y el origen; doble clic en un sketch para editarlo.

**Pruebas manuales pendientes (3c)**

29. **Proyección vinculada**: extruir un rectángulo; nuevo sketch en XY → Proyectar aristas (Mayús+P) → clic en un borde del cuerpo → Terminar. Editar Sketch1 y agrandar el rectángulo: al terminar, la línea proyectada en el segundo sketch se alarga sola. Desbloquearla (K) rompe el vínculo.
30. **Avisos**: suprimir la extrusión de la que salen un sketch en cara o una proyección → las fichas afectadas muestran «!» (motivo en el tooltip) y la barra de estado indica la primera con problemas.

### Fase 4 — Archivos y exportación

Parece mucho, pero son seis bloques que se pueden hacer y probar por separado. La parte realmente difícil es una sola: **4b, convertir el historial a JSON y volver a reconstruirlo igual**. El resto es sobre todo conectar piezas que OCCT, SQLite y Qt ya traen.

| Bloque | Qué se obtiene | Tamaño | Necesita |
|---|---|---|---|
| **4a** Exportar | STL, OBJ y STEP desde Archivo › Exportar | Pequeño | Nada |
| **4b** Serialización | Documento ↔ `.nuf` con pruebas, sin interfaz | **Grande** | Nada |
| **4c** Guardar y abrir | Nuevo, Abrir, Guardar, Guardar como, avisos de cambios | Mediano | 4b |
| **4d** Importar STEP | Sólidos de otros programas en el timeline | Mediano | 4b |
| **4e** Pantalla de inicio | Recientes con miniatura | Mediano | 4c |
| **4f** Recuperación | Autoguardado y restaurar tras un cierre inesperado | Pequeño | 4c |

> Hitos: con **4a** ya se puede diseñar una pieza simple, exportarla e imprimirla (pero se pierde al cerrar). Con **4c** el flujo ya es de uso real. **4d–4f** son mejoras y pueden esperar a otras fases si hace falta.

**Decisiones**

| Tema | Decisión | Por qué |
|---|---|---|
| Biblioteca JSON | nlohmann/json (vcpkg `nlohmann-json`, Fedora `json-devel`, Ubuntu `nlohmann-json3-dev`) | Solo cabeceras, disponible en todos los entornos. Cierra la decisión abierta de la arquitectura (sección 9) |
| Dónde vive la serialización | En `io/`, con funciones por tipo de operación; no como `save()`/`load()` en `Feature` | `document/` sigue sin depender de JSON. Cambia lo que esbozaba la sección 3 de la arquitectura |
| Enumeraciones en el JSON | Como texto (`"Cut"`), nunca como número | Reordenar un `enum` en el código no debe cambiar el significado de archivos viejos |
| Cuándo se escribe en el archivo | Solo al guardar, en una transacción. El archivo se abre para leer o guardar y se cierra enseguida | «No guardar» tiene que seguir funcionando. Además, un archivo abierto todo el rato choca con los bloqueos de Windows y OneDrive. Cambia el «cada cambio se escribe» de la sección 7: ese papel pasa a la copia de recuperación (4f) |
| Geometría en el archivo | No se guarda: al abrir se recalcula el historial. Sin caché BREP en esta fase | Los parámetros son la fuente de verdad. La caché se agrega solo si abrir resulta lento |
| Documentos abiertos | Uno por ventana: Nuevo y Abrir reemplazan el actual tras avisar | Las pestañas de varios diseños serían un trabajo aparte |

**4a — Exportar** ✅ implementado (falta probarlo a mano y en Fedora/CI; ver notas al final del bloque)
- Módulo `geometry/Exchange` (es el único que puede hablar con OCCT): `writeStl`, `writeObj`, `writeStep`, con enlace condicional a las bibliotecas de intercambio, porque se llaman distinto según la versión de OCCT (`TKDESTEP`/`TKDESTL` desde 7.8, `TKSTEP`/`TKSTL` antes). Hay que comprobarlo con el OCCT de Fedora y del CI de Ubuntu.
- **Cuidado:** `BRepMesh` guarda la malla dentro de la forma, y esa forma la comparte con el visor. Se malla sobre una copia para que exportar no altere lo que se ve.
- STL binario por defecto. OBJ con un escritor propio a partir de la triangulación (el de OCCT exige un documento XCAF, no vale la pena). STEP en mm con todos los cuerpos elegidos.
- Diálogo **Exportar**: formato, cuerpos (por defecto los visibles), calidad de malla Baja/Media/Alta/Personalizada, número de triángulos antes de guardar y opción de un archivo por cuerpo. Se exporta el modelo en el punto donde está el marcador.
- Desactivado mientras se edita un sketch o si no hay cuerpos.
- Pruebas: el volumen de la malla coincide con el del sólido (dentro de la tolerancia); la malla es cerrada; STEP exportado y releído mantiene el volumen; rutas con acentos; el visor no cambia tras exportar.
- **Hecho:** `geometry/Exchange` (`writeStl` binario y `writeObj` propios sobre la malla, `writeStep` con `STEPControl_Writer::WriteStream`, `countTriangles`, `meshVolume`), diálogo `ExportDialog` (formato, cuerpos, calidad Baja/Media/Alta, triángulos estimados, un archivo por cuerpo) y Archivo › Exportar activado cuando hay cuerpos y no se edita un sketch o herramienta. 7 pruebas nuevas (`tests/test_exchange.cpp`) pasan en Windows; el STEP solo se comprueba por su texto (no se relee).
- **Pendiente de 4a:** calidad «Personalizada»; compilar en Fedora y en el CI de Ubuntu (el enlace de STEP elige `TKDESTEP` o `TKSTEP` según la versión de OCCT, sin probar con la antigua); probar el diálogo a mano (pruebas 31–32).

**4b — Serialización** ✅ implementado (sin interfaz: se usa desde 4c)

Antes hay que preparar el modelo, porque hoy no se puede reconstruir desde fuera:
- `Sketch::restore(...)`: hoy los ids y sus contadores son privados y `addConstraint` siempre asigna un id nuevo. Los ids deben conservarse tal cual y los contadores guardarse, porque no se reutilizan y los perfiles, ejes y vínculos apuntan a ellos.
- `Document::restore(...)`: carga operaciones, marcador y `nextId_` de una vez, con un solo recálculo. Mantiene el mismo objeto, porque `MainWindow`, `CommandStack` y los paneles están conectados a él (además, insertar de una en una recalcularía de más).
- ~~Imagen de referencia~~ **Descartado:** la imagen solo se muestra mientras se edita el sketch y se quita al terminarlo (no forma parte del modelo), así que no se guarda. Si algún día debe persistir, irá en `blobs`.

Después, el formato:
- `io/DocumentFile`: `saveDocument(Document, ruta)` y `loadDocument(ruta, Document)`, sobre `features`, `meta` y `blobs`, que ya existen en el esquema.
- JSON por tipo: `Sketch` (las 7 geometrías, indicadores, `ModelLink`, restricciones), `SketchAttachment`, `ProfileRef`, `ExtrudeSpec`, `RevolveSpec`, `AxisRef`, `ElementRef`.
- `meta`: nombre, marcador, `nextId`, unidades, versión de la app y estado de vista (cuerpos y sketches ocultos, origen, rejilla, cámara).
- `Guardar` reescribe todas las filas en una transacción (atómica por SQLite). `Guardar como` sobre un archivo existente escribe en temporal y reemplaza, con reintentos si OneDrive lo tiene bloqueado, sin borrar nunca el original antes.
- Comando para ver un `.nuf` como JSON: `NufioCAD --dump archivo.nuf`.
- Pruebas:
  - **Ida y vuelta:** guardar, abrir y comprobar mismas operaciones, mismos volúmenes y **mismos nombres de caras y aristas**, con cada variante de extruir, revolución, sketch en cara y lo proyectado vinculado.
  - **Seguir editando tras abrir:** editar una operación pasada de un archivo reabierto y que las referencias se resuelvan; los ids nuevos no chocan con los viejos.
  - **Archivos dañados:** JSON roto, tipo desconocido, referencia a un id inexistente → mensaje claro, nunca un cierre de la app.
  - **Archivo de referencia** `tests/data/v1.nuf` en el repositorio: si un cambio futuro rompe el formato, la prueba falla. A partir de ahí, cambiar el esquema exige subir `user_version` y escribir una migración.

**4c — Guardar y abrir** ✅ implementado (probado en Windows; falta Fedora y la asociación de `.nuf`)
- Activar Nuevo, Abrir, Guardar y Guardar como (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Mayús+S), que hoy existen desactivados.
- Cambios sin guardar: `*` en el título; aviso Guardar / No guardar / Cancelar al cerrar, abrir o crear otro. `CommandStack` recuerda el punto guardado, para que deshacer hasta ahí vuelva a «sin cambios». Ocultar o mostrar cuerpos también cuenta como cambio.
- Si hay un sketch o herramienta abiertos al guardar: ofrecer terminar y guardar, o cancelar. Lo que se está editando sin confirmar no se guarda a medias.
- Al abrir: limpiar la pila de deshacer, reiniciar el estado de vista, recalcular con cursor de espera y mostrar la lista de operaciones con error. Un archivo con errores **se abre igual** (como en Fusion) y las operaciones quedan marcadas en el timeline.
- `NufioCAD archivo.nuf` desde la línea de comandos y arrastrar un `.nuf` a la ventana.
- **Hecho:** Archivo › Nuevo, Abrir, Guardar y Guardar como (con sus atajos), `*` en el título, aviso Guardar / No guardar / Cancelar al cerrar, abrir o crear otro; `CommandStack::markSaved/isClean` (deshacer hasta el punto guardado vuelve a «sin cambios»; ocultar/mostrar también cuenta); abrir con errores se hace igual y avisa en la barra de estado; abrir por argumento y arrastrando. Comprobado a mano en Windows: abrir por línea de comandos, `*` al cambiar la visibilidad, Ctrl+S lo quita y el aviso al cerrar. Nuevo prueba de la pila de comandos.
- **Decisión:** si hay un sketch o herramienta sin terminar, guardar guarda solo lo confirmado y lo avisa; al cerrar o abrir otro, el aviso dice que esa edición no se guardará (no se «termina y guarda» sola).
- **Pendiente de 4c:** Guardar como con el diálogo y Nuevo no se han probado a mano; el diálogo de Abrir tampoco (solo el argumento de línea de comandos); no se recuerda ni se restaura la cámara.

**4d — Importar STEP ✅**
- `geometry::readStep` con reparación (`ShapeFix`). Las caras se nombran `f<id>:?<n>` con el `Namer` que ya existe.
- Nueva operación `ImportFeature`: una ficha más en el timeline, sin historial propio. Cada sólido es un cuerpo; con varios, la clave de cuerpo es `f<id>:c<n>` (y las caras `f<id>:c<n>:?<k>`, para que no se repitan entre cuerpos y `featuresIn` siga reconociendo la operación).
- La forma se guarda en `blobs` como BREP binario (`BinTools`), no como STEP: el archivo se analiza una sola vez y abrir es rápido. El STEP original se puede mover o borrar sin afectar al diseño.
- Un cuerpo importado se usa como cualquier otro: sketches sobre sus caras, extruir, cortar y unir.
- **Hecho:** Archivo › Importar STEP… (Ctrl+I) y arrastrar un `.step`/`.stp` a la ventana; se puede deshacer; solo se importan sólidos (las superficies sueltas se ignoran y se avisa); el error de un archivo ilegible se explica sin tocar el diseño. 4 pruebas nuevas: ida y vuelta de STEP (volumen y 6 caras nombradas igual cada vez), varios sólidos sin nombres repetidos, archivos inservibles, y diseño completo (importar → sketch en cara → corte → guardar → borrar el STEP → abrir: mismo modelo). El STEP exportado en 4a ya se relee con el mismo volumen.
- **Pendiente de 4d:** unidades distintas de mm (se confía en la conversión de OCCT, sin probar con un STEP en pulgadas); ensamblajes, colores y nombres del STEP (requiere XDE); no se puede reimportar/actualizar desde el archivo original; probado a mano solo con la pieza de muestra de las pruebas (`NUF_SAMPLE_STEP=ruta nuf_tests "[sample]"` la genera), no con STEP de otros programas reales.

**4e — Pantalla de inicio ✅**
- Panel superpuesto al visor vacío al arrancar (mismo mecanismo que la barra de navegación, para no recrear el contexto OpenGL), que desaparece con la primera acción.
- Recientes con miniatura, nombre y fecha; también en Archivo › Abrir reciente. Lista en `QSettings` (máximo 10); los archivos que ya no existen aparecen marcados y se pueden quitar (clic derecho › Quitar de la lista, o se quitan solos al intentar abrirlos).
- Miniatura: `Viewport` renderiza una imagen al guardar y va en `blobs`. Se lee sin recalcular el modelo, así que la lista carga al instante.
- **Hecho:** pantalla con Nuevo diseño / Abrir / Importar STEP y la lista de recientes; se oculta con cualquier primera acción (Nuevo, abrir, importar, crear sketch…) y no vuelve a salir sola; `io::readThumbnail`; la miniatura (400×250 PNG) se guarda en cada Guardar si hay cuerpos. Comprobado a mano en Windows: importar, guardar, reiniciar → el reciente aparece con su miniatura, nombre y fecha; abrirlo desde la lista; Nuevo y Crear sketch ocultan la pantalla; menú Abrir reciente. Prueba nueva de la miniatura en el archivo.
- **Pendiente de 4e:** no se han probado a mano «Quitar de la lista», «Borrar la lista» ni un reciente cuyo archivo ya no existe; el nombre del diseño no cambia al guardar con otro nombre (el recientes muestra el nombre del archivo); la miniatura es una captura del visor tal cual está (con el cubo de vista), no una vista limpia; sin tema claro comprobado.

**4f — Recuperación ✅**
- Cada minuto, si hay cambios, una copia en `QStandardPaths::AppDataLocation/recuperacion/`. Nunca se toca el archivo del usuario.
- Se borra al guardar o cerrar bien. Si al arrancar queda alguna: ofrecer Restaurar / Descartar.
- **Hecho:** cada ventana tiene su copia (`rec-<id>.nuf` + `.origen` con la ruta y el nombre) y un archivo de bloqueo (`QLockFile`): una copia cuyo bloqueo ya no tiene dueño es de una sesión muerta, y una ventana viva de otra instancia no se toca. Cada minuto (`NUF_RECOVERY_SECONDS` lo cambia, para probar) se guarda si hay cambios y se borra si no los hay; también al guardar, abrir, Nuevo y cerrar bien. Al arrancar sale «Restaurar / Descartar / Decidir después» con la más reciente; restaurar abre el diseño con `*` (sigue sin guardar) y a nombre del archivo original, sin añadirlo a recientes. Comprobado a mano en Windows: importar, esperar, matar el proceso, reabrir → aviso → restaurar; cierre normal no deja nada; se limpian los bloqueos sueltos.
- **Registro de cierres inesperados (Windows):** si el programa se cierra por un fallo, guarda en la carpeta `cierres` de los datos de la aplicación (`%APPDATA%/NufioCAD/NufioCAD/cierres`) un informe `.txt` con la pila de llamadas (archivo y línea de nuestro código) y un volcado `.dmp`. `NufioCAD --probar-cierre` lo comprueba. Primer cierre real (4 nov. 2026, dentro de `Qt6Widgetsd.dll`, con la herramienta Extruir abierta sobre un sketch de 20 curvas y 43 restricciones): no se pudo reproducir al azar ni extruyendo ese sketch. **Causa encontrada después** (el propio informe la mostró al reproducir con el polígono: escribir lados, Tab, diámetro y Enter): `ValueInput::setFields` destruía los campos de texto al cambiar de paso mientras uno de ellos aún procesaba el Enter que terminaba la figura. Ahora se retiran y se destruyen después (`deleteLater`). Además, los campos nuevos nacían ocultos y el recuadro quedaba diminuto e impedía escribir; ahora se muestran, y un entero (lados) se ve sin decimales.
- **Pendiente de 4f:** si hay varias copias huérfanas solo se ofrece la más reciente (las otras esperan al siguiente arranque); lo que está a medias en un sketch o herramienta no entra en la copia (como al guardar); sin prueba automática (el módulo vive en la app, que no tiene pruebas); Linux sin probar.

**Fuera de esta fase:** caché de geometría, pestañas con varios documentos, nombres de cuerpos en el STEP (requiere XDE), importar STL/OBJ, y asociar `.nuf` al sistema (va con el instalador).

- **Hecho en 4b:** `Sketch::restore` y `Document::restore`; `io/DocumentFile` (`saveDocument`, `loadDocument`, `dumpDocument`); `NufFile` con transacciones y filas de operaciones; nlohmann-json en vcpkg, CI y README; archivo de referencia `tests/data/v1.nuf` (se regenera solo a propósito con `NUF_WRITE_REFERENCE=1`). 11 pruebas nuevas: ida y vuelta con nombres de caras/aristas idénticos, seguir editando tras abrir, operación que falla al abrir, 8 tipos de archivo dañado, guardado atómico y volcado.
- **Pendiente de 4b:** la cámara no se guarda (solo qué está oculto o visible); compilar en Fedora y el CI de Ubuntu (nlohmann-json es nueva ahí); el reemplazo del archivo al guardar (con reintentos) solo se ha probado en Windows, sin OneDrive bloqueándolo.

**Pruebas manuales al terminar (Fase 4)**

31. **Exportar STL**: pieza con un agujero → Archivo › Exportar → calidad Alta → abrir en el slicer: cerrada y en mm.
32. **Exportar STEP y reimportar**: el mismo volumen, y aparece una ficha «Importación» en el timeline.
33. **Guardar y reabrir**: pieza con sketch en cara, corte y proyección → Ctrl+S → cerrar → abrir: mismo modelo, mismo timeline, y doble clic en una operación pasada sigue editándola.
34. **Cambios sin guardar**: editar → aparece `*` y avisa al cerrar; Ctrl+Z hasta el punto guardado → desaparece.
35. **Archivo entre sistemas**: un `.nuf` guardado en Windows abre igual en Fedora, y al revés.
36. **Recuperación**: editar, esperar más de un minuto, matar el proceso, abrir → ofrece restaurar.
37. **Importar un STEP real**: uno de otro programa (con varios sólidos y en pulgadas, si se puede) → Ctrl+I: aparece la ficha «Importación», cada sólido es un cuerpo, y se puede hacer un sketch en una cara y cortar. Guardar, mover el STEP, abrir: igual.
38. **Recientes**: guardar varios diseños → cerrar → la pantalla de inicio los lista con miniatura; borrar uno desde el explorador → aparece «ya no existe» y se quita; clic derecho › Quitar de la lista.

### Fase 5 — Operaciones de acabado 🚧

Todas siguen el patrón de las operaciones actuales: una clase en `document/Features`, su función en `geometry/Modeling` con nombres de cara estables, serialización en `io/DocumentFile`, una herramienta con vista previa en `app/FeatureTools`, y pruebas.

| Bloque | Qué se obtiene | Tamaño | Estado |
|---|---|---|---|
| **5a** Redondeo y chaflán | Aristas elegidas en el visor, radio/distancia, vista previa | Mediano | ✅ |
| **5b** Shell | Vaciar un cuerpo quitando caras, con espesor | Mediano | ✅ |
| **5c** Agujeros | Sketch de puntos + diámetro, profundidad, avellanado/abocardado | Mediano | ✅ |
| **5d** Patrones | Lineal y circular de una operación o cuerpo | **Grande** | ✅ (cuerpos) |
| **5e** Espejo | Reflejar un cuerpo o una operación sobre un plano | Pequeño | ✅ (cuerpos) |

**5d — Patrones ✅ (de cuerpos)**
- `geometry::pattern`: devuelve las copias del cuerpo (sin el original); la copia k tiene las caras `f<id>:patron<k>:<nombre original>`. Lineal (a lo largo de X/Y/Z, distancia negativa = hacia atrás) o circular (alrededor de X/Y/Z; vuelta completa reparte `count` partes iguales, un ángulo menor va del original a la última copia).
- `PatternFeature`: clave del cuerpo + tipo + eje + cantidad (con el original, 2–200) + distancia o ángulo + operación. **Unir** funde las copias con el cuerpo una a una (si se solapan se unen en una pieza; si no, quedan como piezas del mismo cuerpo); **Cuerpo nuevo** las junta en un cuerpo aparte. Se guarda como `pattern`.
- Herramienta `PatternTool`: Sólido › Patrón en la cinta, con vista previa en vivo.
- **Hecho:** 3 pruebas nuevas (lineal separado, solapado y como cuerpo nuevo hacia atrás; circular 4×90° y medio giro; seguir a la pieza, errores y guardar/abrir). Comprobado a mano: botón, panel y vista previa.
- **Pendiente de 5d:** repetir **operaciones** (no solo cuerpos) y patrón por trayectoria. (Los ejes propios ya llegaron con 6b.)

**5e — Espejo ✅ (de cuerpos)**
- `geometry::mirror`: copia del cuerpo reflejada sobre un plano; se reorienta el sólido (un espejo lo deja «del revés») y las caras se llaman `f<id>:espejo:<nombre original>`.
- `MirrorFeature`: clave del cuerpo + plano (de origen XY/XZ/YZ o una cara plana, con la misma estructura que el sitio de un sketch, que sigue a la cara si la pieza cambia) + operación. **Unir** lo junta con lo que toca (el propio cuerpo si toca el plano; las caras coplanares se funden) y **Cuerpo nuevo** deja la copia aparte. Se guarda como `mirror`.
- Herramienta `MirrorTool`: Sólido › Espejo en la cinta; elige cuerpo, plano (o una cara plana en la vista) y operación, con vista previa en vivo.
- **Hecho:** 3 pruebas nuevas (unir → caja de 40×10×5 con 6 caras y el doble de volumen; cuerpo nuevo; cara como plano; seguir a la pieza al hacerla más alta; error si el cuerpo no existe; guardar y abrir). Comprobado a mano: el botón, el panel y la vista previa.
- **Pendiente de 5e:** reflejar **operaciones** (no solo cuerpos) y varios cuerpos a la vez; planos de construcción (llegan en la Fase 6).

**5c — Agujeros ✅**
- `geometry::holeTool`: un cilindro por centro, hacia dentro del material, que empieza 1 mm fuera de la superficie para un corte limpio; **simple**, **abocardado** (cabeza cilíndrica) y **avellanado** (cono con ángulo), con profundidad o «atravesar todo». Las paredes se llaman `f<id>:pared`.
- `HoleFeature`: un sketch cuyos **círculos** (su centro) y puntos marcan dónde van los agujeros, más el tipo y las medidas; se resta con la misma lógica que un corte (avisa si no toca nada). Se guarda como `hole`.
- Herramienta `HoleTool`: Sólido › Agujero (H): elige el sketch (por defecto el último con círculos), tipo, diámetro, profundidad o atravesar, cabeza y «al otro lado del plano»; vista previa en rojo.
- **Hecho:** 3 pruebas nuevas con volúmenes exactos (simple, atravesado, dos agujeros, abocardado; avellanado válido y mayor), errores claros (sin círculos, cabeza menor que el agujero) y ida y vuelta por el archivo. El botón aparece en la cinta.
- **Pendiente de 5c:** no hay herramienta de **punto** en el sketch (hoy se usan círculos); colocar agujeros haciendo clic directo en una cara; roscas; la herramienta no se probó a mano en la ventana (solo que abre y aparece en la cinta).

**5b — Vaciado (shell) ✅**
- `geometry::shell` con `BRepOffsetAPI_MakeThickSolid`: las caras elegidas quedan abiertas y el resto se vuelve pared del espesor dado, hacia dentro o hacia fuera. Las paredes nuevas se llaman `f<id>:interior:<cara de origen>`; las demás caras, incluida la que queda como borde de la abertura, conservan su nombre.
- El kernel devuelve el cuerpo intacto, sin error, cuando el espesor es imposible; se detecta y se informa («prueba con un espesor menor»).
- `ShellFeature` (caras por nombre, espesor, dirección), como `shell` en el `.nuf`; si una cara ya no existe se avisa y se hace con las demás.
- Herramienta `ShellTool`: Sólido › Vaciado (cinta, grupo MODIFICAR). Los clics en caras agregan o quitan, las elegidas se resaltan en azul translúcido (nuevo `Viewport::showToolFaces`) y el resultado se ve en vivo.
- **Hecho:** 3 pruebas nuevas (copa de 20×10×5 con paredes de 1: volumen exacto, 5 caras interiores nuevas y las exteriores con su nombre; hacia fuera; espesor demasiado grande, sin caras y espesor cero; seguir a la pieza al hacerla más alta, guardar y abrir). Comprobado a mano en Windows: elegir la cara de arriba, espesor 2, vista previa.
- **Pendiente de 5b:** vaciar sin abrir ninguna cara (hueco cerrado); espesores distintos por cara; el vaciado hacia fuera solo se probó con la caja; aceptar con el botón se probó en 5a (mismo código) pero no en esta ronda; Linux sin probar.

**5a — Redondeo y chaflán ✅**
- `geometry::fillet` y `geometry::chamfer` sobre `BRepFilletAPI` con las aristas por índice; las cadenas de aristas tangentes se siguen solas. Las caras nuevas se llaman `f<id>:redondeo:<arista>` / `f<id>:chaflan:<arista>` (esquinas: `f<id>:esquina:?<n>`) y las demás conservan su nombre, así que un sketch sobre una cara anterior sigue funcionando.
- `FilletFeature` y `ChamferFeature` (base `EdgeFeature`): aristas por nombre + medida. Si una arista ya no existe se avisa y se hace con las demás; si ninguna, error. Se guardan como `fillet`/`chamfer` en el `.nuf`.
- Herramienta `EdgeTool`: Sólido › Redondeo (F) y Chaflán. Cada clic en una arista la agrega o la quita; las elegidas se resaltan en azul sobre el cuerpo y el visor muestra el resultado en vivo; «Quitar selección». Editar la operación (doble clic en la ficha) conserva sus aristas.
- **Hecho:** 6 pruebas nuevas (volúmenes exactos de redondeo y chaflán, nombres de las caras nuevas, radio imposible, seguir a la pieza al editar la altura, arista desaparecida con aviso, guardar y abrir). Comprobado a mano en Windows: elegir 3 aristas, cambiar el radio, vista previa y aceptar.
- **Pendiente de 5a:** chaflán con dos distancias o distancia y ángulo; radio variable; elegir una cara para redondear todas sus aristas; en edición las aristas no se marcan en el visor hasta que se cambian (solo se resaltan en azul); sin probar en Linux; la prueba con un STEP real aún no.

### Fase 6 — Geometría de referencia y parámetros
- **6a Planos de construcción ✅:** `ConstructionPlaneFeature` (plano base —de origen, una cara plana u otro plano de construcción—, giro alrededor del eje X o Y del plano y desfase a lo largo de su normal; se publica en `ModelState::planes`). Un sitio (`SketchAttachment`) puede apuntar a un plano de construcción (`reference`), así que sirven para sketches y para el plano de simetría del espejo, y siguen al plano si se edita. Se ven como cuadrados translúcidos turquesa (tamaño según la pieza), con carpeta «Planos de construcción» en el navegador (casilla para ocultar) y se eligen en «Crear sketch». Herramienta Sólido › Plano (grupo CONSTRUIR). Se guarda como `plane`. 3 pruebas nuevas.
- **6b Ejes de construcción ✅:** `ConstructionAxisFeature` (intersección de dos planos —de origen, cara o de construcción— o, con uno solo, la perpendicular a él por su origen; planos paralelos dan error). Se publica en `ModelState::axes`, se ve como una línea turquesa y se usa como eje de **Revolución** (`AxisRef::Kind::Construction`) y de **Patrón** (lineal: su dirección; circular: eje de giro, ya sin limitarse al origen). Herramienta Sólido › Eje; carpeta «Ejes de construcción» en el navegador. Se guarda como `axis`. 2 pruebas nuevas. Con esto el patrón y la revolución ya aceptan ejes propios.
- **6c Parámetros y expresiones ✅:** tabla de parámetros del documento (`Document::parameters`, se guarda en la tabla `parameters` del `.nuf`; cambios por el comando `SetParameters`, con deshacer). Evaluador propio (`Expression.hpp`): números con punto o coma, `+ - * / ^`, paréntesis, otros parámetros, funciones (`sin cos tan asin acos atan sqrt abs round floor ceil min max pow`; argumentos con `;`; trigonometría en grados), constantes `pi` y `e`, unidades tras un número (`mm cm m in deg rad`), referencias circulares detectadas. Las operaciones guardan una expresión por campo (`Feature::expressions`, campos declarados en `numericFields()`): extrusión (distancias y desfase), revolución (ángulos), redondeo/chaflán (tamaño), vaciado (espesor), agujeros (diámetros, profundidades, ángulo), patrón (distancia, ángulo, cantidad) y plano de construcción (distancia, ángulo). Al calcular se evalúan sobre una copia, así que al cambiar un parámetro todo lo ligado se recalcula; si falta o falla un parámetro la operación queda en error con un mensaje. En la interfaz, los cuadros numéricos de las herramientas aceptan expresiones (`ExpressionSpinBox`: «alto*2» muestra el valor en el tooltip y la expresión se conserva al editar); botón Sólido › Parámetros (grupo CONSTRUIR) con la tabla, valores en vivo y avisos. 3 pruebas nuevas.
- **Renombrar parámetros ✅ (backlog, oleada 1):** al cambiar el nombre de una fila en Sólido › Parámetros, las expresiones de las demás filas se reescriben al instante y, al aceptar, también las de las operaciones (`renameInExpression`, `Document::setParameters(tabla, renombrados)`; el comando `SetParameters` guarda las expresiones anteriores y se deshace entero). Funciona token a token (no toca `grosor_2` al renombrar `grosor`, ni funciones ni unidades). 1 prueba nueva ([params]). Probado solo con pruebas automáticas (pasos 6.17–6.19 de la guía, sin hacer a mano).
- **Cotas del sketch ligadas a parámetros ✅ (backlog, oleada 1):** el cuadro de una cota (doble clic) acepta un número o una expresión (`radio`, `grosor*2`; longitudes en la unidad elegida, ángulos en grados). Mientras se edita, la expresión vive en `Constraint::expression`; al terminar el sketch pasa a las expresiones de la operación (clave `c<id>`, como cualquier otro campo ligado), así que se guarda en el `.nuf`, se renombra con el parámetro y se deshace igual. `SketchFeature::numericFields` expone las cotas con valor; al calcular, la operación evalúa las expresiones y **vuelve a resolver el sketch** (si el solver no converge o hay conflicto, la operación queda en error con el motivo; radios, diámetros y longitudes deben ser > 0; se conserva el signo de las distancias en X/Y). Al reabrir el sketch se parte del ya calculado con las expresiones puestas en sus cotas. 1 prueba nueva (círculo con radio ligado: cambio de volumen, renombrado, guardar y abrir, error y recuperación).
- **Aviso de compilación (Windows en español):** MSVC escribe «Nota: inclusión del archivo» y ninja no la reconoce, así que **no detecta cambios en cabeceras** y deja objetos viejos enlazados con nuevos (síntoma: caídas absurdas en pruebas que antes pasaban). Compilar siempre con `VSLANG=1033` (salida de MSVC en inglés) y, si hubo cambios de cabecera sin esa variable, reconstruir con `--clean-first`.
- **Pendiente de 6c:** la cota ligada no se distingue a simple vista en el sketch (falta una marca, p. ej. «ƒ»); editar una cota ligada con un número suelto quita la expresión sin avisar; el diálogo no permite intercambiar dos nombres a la vez (a↔b se bloquea por «repetido»); solo cotas con valor (no restricciones como la curvatura); no probado a mano en la ventana.

### Fase 7 — Generadores
- **7a Engranajes ✅:** `geometry::gear` (perfil de involuta con splines por flanco, cabeza y raíz en arcos, tramo radial si la base queda sobre la raíz; recto por prisma, helicoidal por secciones giradas en un solo sólido; agujero central) y `GearFeature` (plano de origen o de construcción, módulo, dientes 6–300, ángulo de presión 5–35°, ancho, agujero, ángulo de hélice hasta 45°, operación nuevo cuerpo/unir; todos ligables a parámetros). Caras con nombre estable: `f<id>:lado:d<i>:flanco_d|flanco_i|cabeza|raiz|rad_d|rad_i`, `…:lado:agujero`, `…:inicio`, `…:fin`. Herramienta Sólido › Engranaje (grupo GENERAR). Errores claros (dientes puntiagudos o solapados, agujero demasiado grande). 3 pruebas nuevas.
- **Pendiente de 7a:** engranajes internos y cremallera, redondeo de raíz, perfil de cicloide, juego/corrección de dentado.
- **7b Roscas ✅:** `geometry::thread` (varilla roscada métrica ISO aproximada: cresta P/8, fondo P/4, profundidad 0,6134 P; derecha o izquierda; el corte transversal es una curva cerrada que se gira a lo largo del eje y se une en una superficie spline suave, 8 secciones por vuelta; ver la optimización más abajo) y `ThreadFeature` (plano, diámetro, paso, largo, operación nuevo cuerpo/unir/cortar; con «Cortar» sobre un agujero del diámetro menor da una rosca interior; ligable a parámetros). Herramienta Sólido › Rosca con medidas estándar M3–M24 o propias; avisa del diámetro menor para el agujero. Caras `f<id>:rosca`, `:inicio`, `:fin`. 2 pruebas nuevas.
- **Optimización de la rosca ✅ (auditoría de rendimiento):** causa principal: `BRepLib::OrientClosedSolid` lanzaba rayos contra cientos de superficies spline y tardaba segundos (más que construir la rosca); además la rosca era un loft con 130–400 caras (cada una con su curva de 64 puntos). Ahora la rosca es **una superficie spline suave (cúbica en el eje, 8 secciones por vuelta, 32 puntos por sección, esquinas del perfil suavizadas) más dos tapas = 3 caras**, y la orientación hacia fuera se decide con una normal en un punto. Resultado en Debug: de 1,6–7,7 s a **0,03–0,06 s** por rosca, sin depender del largo. La vista previa de Rosca y Engranaje espera 250 ms tras el último cambio (no recalcula a cada tecla). (`Shape::bounds` se probó con el ajuste exacto de la caja, pero tardaba decenas de segundos con una rosca: se volvió a la caja rápida, que incluye los puntos de control y sale hasta un 10 % mayor en una rosca.)
- **Optimización del engranaje helicoidal ✅:** el agujero ya no se hace con una booleana contra la superficie lateral (casi la mitad del tiempo): el engranaje helicoidal se arma con la superficie lateral del loft, las dos tapas con su agujero y la pared del agujero, cosidas en un sólido; la orientación se decide con la normal de una cara de la cabeza de un diente. En Debug, 20 dientes con hélice y agujero: de 2,7 s a ~1,5 s; 40 dientes: de 7,2 s a ~3,7 s (las mediciones varían con la carga del equipo). El engranaje recto (prisma) ya iba bien: 0,2–0,8 s. 1 prueba nueva ([gear]: sentido del ancho, hueco del agujero).
- **Pendiente del engranaje:** lo que queda es el loft suave de todas las aristas del perfil (≈ 3/4 del tiempo con muchos dientes); se podría hacer cada diente por separado y repetirlo girado, o dejar la vista previa en un perfil más simple mientras se teclea.
- **Congelamiento al calcular la rosca (causa real) ✅:** no era la geometría sino el **mallado para dibujarla**: una rosca M8×10 con la desviación relativa por defecto del visor (0,001 del tamaño) daba ~106 000 triángulos y 18 s en Debug (la de 40 mm: 680 000 triángulos, 279 s ≈ 4,5 min; la versión anterior de 130 caras daba 375 000 triángulos y 7 s). El visor ahora malla con desviación absoluta de 0,1 mm los cuerpos con alguna superficie spline de más de 1500 puntos de control (≈ 10 000 triángulos, ~2 s en Debug, y se ve lisa por el sombreado).
- **Medido dentro de la app (cronómetro temporal, ya quitado):** al abrir Rosca la interfaz se paraba **28 s**: 27 s eran de `Shape::bounds`, que yo había cambiado a `AddOptimal` (muy lento con la superficie spline de la rosca) y que el visor llama al refrescar; se revirtió (la caja de una rosca vuelve a incluir los puntos de control, ≈ 10 % mayor). Después, el dibujo del cuerpo: la rosca se parte en tramos de 1,5 vueltas (el mallado crece más que linealmente con el tamaño de cada cara: 17 s en una cara de 8 vueltas → 3 s en tramos). Resultado en Debug: abrir Rosca o cambiar un valor ≈ **3 s** (antes 28 s), largo 30 ≈ 4 s, largo 60 ≈ 7 s.
- **Dibujo de cuerpos pesados en segundo plano ✅:** `geometry::DisplayMesh` (`prepareMesh` en un hilo de trabajo sobre una copia: mallado en paralelo con desviación absoluta de 0,1 mm y normales de cada nodo; `applyMesh` en el hilo de la interfaz solo pasa punteros a las caras del cuerpo original; `hasHeavySurface` = más de 2000 puntos de control en caras spline). El visor, ante un cuerpo pesado sin malla, la prepara en segundo plano, sigue mostrando lo anterior (barra de estado: «Preparando el dibujo de la pieza…») y lo pinta al terminar; si llega otra petición, cancela la anterior; al cerrar espera a los hilos. Medido en la app (Debug, cronómetro y vigilante del bucle de eventos temporales): **sin pausas de la interfaz** al abrir Rosca y al cambiar diámetro o largo (10, 30, 60; tres cambios seguidos); el dibujo nuevo aparece a los 0,3–1,3 s. Cerrar la app durante el cálculo sale limpio (código 0). 1 prueba nueva ([thread]).
- **Pendiente del segundo plano (dibujo):** el dibujo antiguo se ve unos instantes mientras se prepara el nuevo; el borde de las caras de una rosca se dibuja sin la malla de aristas (lo discretiza el visor). (Los cálculos de geometría, rosca y booleanas, ya van en segundo plano: ver «Cálculo del historial y vistas previas en segundo plano».)
- **Cálculo del historial y vistas previas en segundo plano ✅ (pedido en las pruebas: cilindro Ø10 con una rosca cortante encima; al cambiar el diámetro a 20 y volver a 10 la app se quedaba colgada):** `Document::enableBackgroundCompute` (la ventana lo activa; las pruebas y los documentos de trabajo siguen calculando en el acto): cada cambio del historial copia lo que hace falta (operaciones desde la primera afectada, el estado anterior y los valores de los parámetros) y recalcula en un hilo aparte; la interfaz sigue viva viendo el resultado anterior (barra de estado: «Calculando el modelo…»), un cambio nuevo **cancela** el cálculo en curso (`geometry::CancelScope`: las booleanas se interrumpen a media operación) y al acabar se instala el resultado y se avisa con `changed` (`computingChanged` al empezar y al terminar). Mientras se calcula no se abren herramientas, sketches ni exportación (avisa «El modelo todavía se está calculando»); deshacer, rehacer, mover el marcador y editar siguen disponibles. Abrir un archivo e importar un STEP también calculan en segundo plano (el encuadre y el resumen salen al terminar). Las **vistas previas** de las herramientas y de los generadores (`BackgroundRun`) también: se arma la operación con los valores de los cuadros, se calcula en un hilo sobre copias y se dibuja al terminar; «Aceptar» queda deshabilitado mientras tanto y aparece «Calculando…» si pasa de 300 ms. En la prueba de interfaz, cambiar el diámetro del cilindro con la rosca encima paró la ventana **60 ms** (antes, lo que tardaba la rosca). 3 pruebas nuevas ([background]) y 1 paso nuevo de la prueba de interfaz.
- **Pendiente del cálculo en segundo plano:** el cálculo en curso solo se interrumpe dentro de las booleanas (la construcción de la rosca, un engranaje helicoidal o un redondeo grande terminan antes de poder cancelarse, aunque su resultado se descarta); no hay barra de progreso ni botón «Cancelar» (solo se cancela cambiando algo); el hilo de cálculo y el visor comparten los cuerpos ya dibujados (solo lectura): se comprobó en las pruebas, pero no con piezas muy grandes ni en Release; el dibujo en pantalla del resultado (mallado de cuerpos que no son «pesados») sigue en el hilo de la interfaz y en generadores grandes todavía da paradas de 1–2 s; el cálculo corre en un solo hilo por documento (las operaciones independientes del historial no se reparten); los generadores calculan en su propio documento de trabajo, así que al aceptar se vuelve a calcular dentro del documento.
- **(Resuelto con el dibujo en segundo plano)** El tiempo que quedaba al abrir la herramienta Rosca (~3 s en Debug, casi todo el visor calculando normales) ya no para la interfaz.
- **(Resuelto)** El mallado dejó el hilo de la interfaz (ver «Dibujo de cuerpos pesados en segundo plano»); queda medir en Release.
- **Pendiente de 7b:** Unir/Cortar con un cuerpo sigue costando ~2 s en Debug (todo en la booleana de OpenCASCADE: intersección de planos con la superficie spline); probar en Release y, si hace falta, hacer la rosca en 4 caras (cresta, flancos, fondo) para simplificar la intersección; el engranaje helicoidal usa el mismo tipo de loft y puede tener el mismo problema; rosca sobre una cara cilíndrica existente, perfiles no métricos (trapezoidal, UNC) y chaflán de entrada.
- **7c Barrido (sweep) ✅:** `geometry::sweep` (contornos del perfil con `BRepOffsetAPI_MakePipeShell`; trayectoria = curvas de otro sketch que formen un camino continuo; esquinas redondeadas; orientación que sigue la trayectoria o fija; huecos del perfil restados) y `SweepFeature` (perfiles + sketch y curvas de la trayectoria + operación). Caras `f<id>:lado:<origen>`, `:inicio:<perfil>`, `:fin:<perfil>`. Herramienta Sólido › Barrido (grupo CREAR): se eligen en la vista las regiones del perfil y las curvas de la trayectoria. Errores claros si el camino está roto. 2 pruebas nuevas (recto, en L, camino roto, guardar).
- **Pendiente de 7c:** guías/secciones intermedias, torsión y escala a lo largo del camino, trayectoria con aristas de un cuerpo; probado a mano solo que el botón aparece (el flujo completo con dos sketches está cubierto por las pruebas).
- **7d Loft ✅:** `geometry::loft` (`BRepOffsetAPI_ThruSections` por el contorno exterior de cada perfil, en orden; superficie suave o recta entre perfiles) y `LoftFeature` (lista ordenada de perfiles + tipo + operación). Caras `f<id>:lado:<origen en el primer perfil>`, `:inicio`, `:fin`. Herramienta Sólido › Loft (grupo CREAR): el orden es el de elección en la vista. Pide al menos dos perfiles. 2 pruebas nuevas (tronco de pirámide con volumen exacto, tres perfiles, guardar).
- **Pendiente de 7d:** guías y curvas centrales, condiciones de tangencia en los extremos, perfiles con huecos, elegir el vértice de arranque; sin probar a mano el flujo completo en la interfaz.

### Fase 8 — Multi-cuerpo y ensambles
- **8a Mover, copiar y combinar cuerpos ✅:** `geometry::moveBody` (giro alrededor de un eje + desplazamiento; al mover conserva el nombre de las caras, al copiar las llama `f<id>:copia:<nombre>`), `MoveFeature` (cuerpo, X/Y/Z, eje de origen o de construcción, el eje pasa por el centro del cuerpo o por el origen, ángulo, copia sí/no) y `CombineFeature` (cuerpo destino + cuerpos herramienta + unir/cortar/intersecar + conservar herramientas). Herramientas Sólido › Mover cuerpo (un clic en una cara elige su cuerpo) y Combinar cuerpos (grupo MODIFICAR); valores ligables a parámetros. 2 pruebas nuevas.
- **Pendiente de 8a:** mover con un gizmo/flechas arrastrables en la vista; mover por la distancia entre dos puntos o caras; combinar eligiendo los cuerpos en la vista; dividir un cuerpo en piezas; sin probar a mano el flujo completo (solo que los paneles abren).
- **8b Nombres y componentes ✅:** `Document::bodyInfo` (nombre propio y componente por clave de cuerpo; los nombres sustituyen a «Cuerpo1»… en todos los estados sin recalcular y sobreviven a cambios del historial), comando `SetBodyInfo` (con deshacer), guardado en el archivo (meta `cuerpos`) y diálogo Sólido › Componentes (grupo ORGANIZAR) con la tabla Cuerpo / Nombre / Componente. En el navegador cada componente es una carpeta con casilla que muestra u oculta todos sus cuerpos. 1 prueba nueva (más un error real encontrado y corregido: iterar `items()` de un JSON temporal).
- **Pendiente de 8b:** los componentes son solo una agrupación (nombre y visibilidad): no tienen origen propio ni se pueden mover o instanciar varias veces como en Fusion; crear un componente desde el navegador (clic derecho) o arrastrando cuerpos; exportar «un archivo por componente»; la carpeta «Cuerpos» queda vacía cuando todos están en componentes.
- **8c Uniones ✅:** `JointFeature` (cara fija + cara móvil de dos cuerpos distintos; el cuerpo de la cara móvil se coloca con su cara enfrentada a la fija, centro con centro; tipos **Rígida** (distancia), **Giratoria** (distancia y ángulo alrededor de la normal) y **Deslizante** (distancia y desplazamiento en X de la cara fija); opción «mismo lado»; valores ligables a parámetros) con `geometry::placeBody` (conserva los nombres de las caras, así que se rehace sola si cambian las piezas). Herramienta Sólido › Unión (grupo ORGANIZAR): un clic elige la cara fija, el siguiente la móvil (otro clic en una elegida la quita) y hay botón para intercambiarlas. Error claro si las dos caras son del mismo cuerpo o no son planas. 2 pruebas nuevas (apoyar, separar, girar 90°, deslizar, seguir a la pieza, error, guardar).
- **Pendiente de 8c:** uniones con caras **cilíndricas** (eje de un perno en un agujero) y con aristas o puntos; resaltar en la vista las dos caras elegidas; las uniones colocan una vez (no hay movimiento/animación ni límites de giro); dos o más uniones en cadena están cubiertas solo por la lógica, no por pruebas; sin probar a mano el flujo completo de elegir caras.

### Fase 9 — Pulido para impresión 3D
- **9a Revisión para imprimir ✅ (antes de exportar):** `geometry::checkPrintable` y, en el diálogo Archivo › Exportar (STL/OBJ), un resumen automático de los cuerpos marcados que se recalcula al cambiar la elección o tu impresora: ✓ todo en orden, o avisos ⛔/⚠/ℹ con el motivo. Revisa: sólido sin volumen, inválido o con aristas abiertas (no hermético); **paredes más finas que la boquilla** (no se imprimen) o de menos de dos líneas (salen débiles), entre caras planas paralelas; **cuerpos que se solapan**; y que la pieza **quepa en la cama** (pudiendo girarla). Sección «Mi impresora» en el mismo diálogo (boquilla y cama X×Y×Z, por defecto 0,4 mm y 256³) que se recuerda entre sesiones. No corrige nada: el aviso no bloquea exportar. 2 pruebas nuevas.
- **Pendiente de 9a:** las paredes solo se detectan entre caras **planas** paralelas (no entre cilindros ni superficies curvas, y a veces avisa de más en esquinas); no revisa voladizos/soportes, orientación de impresión ni detalles muy pequeños; con más de 1500 caras no revisa paredes; se salta para STEP (no es malla); el aviso no señala dónde está el problema en la vista 3D; solapes de cuerpos con una booleana, que en piezas complejas puede tardar.
- **9b Imprimir ✅ (abrir el slicer):** Archivo › **Imprimir…** (Ctrl+P): el mismo diálogo que Exportar pero siempre STL, con la revisión de 9a y el campo «Slicer» (programa elegido con «Elegir…», se recuerda). Al aceptar escribe el STL en la carpeta temporal (`%TEMP%\NufioCAD\<diseño>.stl`) y abre el slicer con el archivo. «Imprimir» queda desactivado hasta elegir un programa que exista. Probado a mano de punta a punta (con el Bloc de notas como «slicer»).
- **Pendiente de 9b:** no manda el trabajo al laminador con perfil o filamento (solo abre el archivo); no hay formato 3MF; probado con el Bloc de notas, no con Bambu Studio u Orca reales; en Linux/Fedora la elección del programa usa el filtro `*.exe` (hay que escoger «Todos los archivos»).
- **9c Medir ✅:** botón **Medir** (grupo INSPECCIONAR). Con una cara o arista da área, longitud, radio, diámetro, centro o normal; con dos, distancia mínima, diferencia en X/Y/Z, ángulo, paralelismo y distancia entre ejes o centros de agujeros. El resultado sale en un panel sobre la vista; Esc termina.
- **Pendiente de 9c:** no dibuja la línea de la medida sobre el modelo; mide solo caras y aristas (no vértices ni puntos libres); con más de dos elementos usa los dos últimos de la lista, no por orden de clic; los números usan punto decimal; no copia el resultado al portapapeles.
- **9d Sección ✅:** botón **Sección** (INSPECCIONAR). Corta la vista de los cuerpos con un plano XY, XZ o YZ a la posición elegida, tapa el corte en azul y permite ver el otro lado. Solo cambia la vista: el modelo, el timeline y la exportación no se tocan. Esc termina.
- **Pendiente de 9d:** probado a ojo con una caja (los tres planos, tapa azul, «otro lado» y Esc); falta comprobar el rendimiento con piezas grandes y cómo se ve la tapa en piezas con huecos; solo planos de origen (no caras ni planos de construcción); un solo corte a la vez; no se guarda en el archivo; no hay análisis de calidad de malla (se deja para más adelante).
- **Sin hacer de la Fase 9:** control de calidad de malla al exportar (la revisión de 9a mira el sólido, no la malla). El envío al slicer ya está en 9b.

### Unidad como etiqueta en los cuadros numéricos ✅ (pedido en las pruebas)
La unidad («mm», «°») ya no forma parte del texto del cuadro: es una etiqueta fija a la derecha del número (`setUnit` en `ExpressionSpinBox.hpp`, sirve para cualquier cuadro numérico), así que al editar solo se selecciona y reescribe el número. Se aplica a las herramientas de operación (distancias, ángulos, diámetros, paso, largo…), la Sección, el diálogo de Exportar/Imprimir (medidas de la impresora) y la altura del texto del sketch. Si se escribe «10 mm» a mano, se sigue entendiendo.
- **Unidades configurables ✅:** Archivo › **Configuración…** → unidad de longitud (mm, cm, m, in; se recuerda). `Units` (singleton con señal `changed`): el modelo, el archivo y los cálculos siguen en mm; solo cambia lo que se ve y se escribe. Los cuadros de longitud (`ExpressionSpinBox::setLength`: el valor sigue en mm, el texto y la etiqueta cambian al instante; un número a secas se entiende en la unidad elegida, con unidad o parámetros ya viene en mm; los pasos de las flechas son de 1 unidad), la Sección, la impresora del diálogo Exportar/Imprimir, la altura del texto, los textos de estado de las herramientas, el cursor del sketch (X/Y), las **cotas del sketch** (`setDimensionLengthScale`), el cuadro de valor de una cota, el ancho de la imagen de referencia y los valores que se escriben junto al cursor al dibujar (por campo: longitudes sí; ángulos, lados, copias y factores no). 1 prueba nueva ([annotations]); comprobado a ojo en cm y en pulgadas.
- **Pendiente de las unidades:** siguen en mm el panel de Medir, la revisión para imprimir (avisos), la lista de parámetros y los mensajes de error de la geometría; las unidades de superficie y volumen; la rejilla del sketch; las unidades por documento (hoy es una preferencia de la aplicación, no del archivo). Escribir una expresión con parámetros en cm requiere poner la unidad (`grosor + 2 cm`).

- **Rosca interior y colocación de cuerpos generados ✅ (pedido en las pruebas):** la rosca y el engranaje ya no van solo en el origen del plano: «Plano o cara» (origen, plano de construcción o **cara plana elegida con un clic en la vista**, que la sigue si la pieza cambia) y **Posición X / Y** en ese plano (al elegir una cara se pone en el centro de la cara; editable, ligable a parámetros y se guarda en el archivo). El signo del largo (o del ancho del engranaje) da el sentido, como en Extruir: al elegir una cara, «Cortar» pone el signo negativo (hacia dentro de la pieza) y las demás operaciones el positivo; se puede cambiar a mano. Rosca: operación «Cortar (rosca interior)» y campo **Holgura** (radial, 0,15 mm por defecto: el agujero sale de Ø + 2·holgura, para que un tornillo impreso entre). Engranaje: se añade «Cortar». Cambios: `x`, `y` (y `clearance` en la rosca) en `GearFeature`/`ThreadFeature`, y un `Placement` común en `FeatureTool`; ningún cambio en la geometría ni en el resto de operaciones. Los archivos anteriores se abren (x, y y holgura valen 0). 2 pruebas nuevas.
- **Pendiente de la colocación:** no hay flecha para arrastrar el largo en la vista (como en Extruir); la posición se escribe o se toma del centro de la cara (falta elegir un punto con el ratón, o el centro de un agujero o círculo existente); las demás piezas generadas (planos, ejes, patrones) siguen sin posición propia; en un tornillo normal (Unir/Cuerpo nuevo) la holgura no se usa.

### Fase 10 — Herramientas generadoras (tipo MakerLab)
Asistentes que hacen preguntas («redonda o rectangular», grosor de paredes, grosor del frente, profundidad, alto, ancho…) y **generan un diseño normal** en el timeline, con las respuestas como parámetros con nombre: se puede seguir editando con el CAD después. Un solo motor, dos entradas: tarjeta «Herramientas» junto a «Diseñar desde cero» sobre el lienzo vacío, y pestaña HERRAMIENTAS en la cinta.
- **10a Base común:** formulario de preguntas con vista previa en vivo (esperando a que se deje de escribir) y generación del historial con parámetros; resultado agrupado en un componente.
- **10a Base común ✅:** `document::Generator` (preguntas `GeneratorField`: longitudes y opciones, con campos que solo se muestran según otra opción; y `make(valores, sufijo)` → `Recipe` con parámetros con nombre, operaciones del historial ligadas a ellos por expresiones, nombres de cuerpos y componente), `freeSuffix` (si el diseño ya tiene esos parámetros, usa `_2`, `_3`…), `applyRecipe` y el comando `AddRecipe` (todo en un solo paso de deshacer/rehacer). Interfaz: pestaña **HERRAMIENTAS** en la cinta y menú **Herramientas**; `GeneratorTool` (en el panel de propiedades, como las otras herramientas): formulario con los cuadros de longitud (unidad configurable), **vista previa en vivo** (espera 250 ms; el diseño se calcula en un documento aparte y se ve junto a lo que ya hay; avisa del error de la operación que falle) y, al aceptar, se aplica la receta. Para que lo generado sea de verdad paramétrico se añadieron las primitivas **Caja** y **Cilindro** (`BoxFeature`/`CylinderFeature`: ancho, fondo, alto con signo, posición X/Y/Z, centrada o simétrica, operación; ligables a parámetros; sobre un plano o una cara; con sus herramientas en CREAR y editables con doble clic en el historial). 4 pruebas nuevas.
- **Reabrir un generador ✅:** `Feature::origin` (`GeneratorOrigin`: id del generador, sufijo de parámetros, respuestas codificadas con `encodeValues` y `lead` = id de la primera operación del resultado) marca todas las operaciones que sacó una receta y se guarda en el `.nuf` (`params.origin`). Clic derecho en una de ellas › «Reabrir «…»…» abre el formulario con las respuestas de entonces (`GeneratorTool` con `lead`/valores iniciales; la vista previa oculta lo anterior) y al aceptar el comando `ReplaceRecipe` lo rehace en el mismo sitio del historial con un solo recálculo: si la receta nueva tiene las mismas operaciones se sustituyen una a una conservando ids; si no, se rehace el grupo entero, y eso solo se permite si ninguna operación de fuera depende de ellas (si no, aviso en la barra de estado). Los parámetros de la receta cambian de valor en su sitio. 2 pruebas nuevas.
- **Pendiente de reabrir un generador:** lo que el usuario edite a mano dentro del grupo (cambiar una operación del generador con su propio panel) se pierde al reabrir; no avisa de eso; los diseños hechos antes de este cambio no traen el origen (no se pueden reabrir); no hay icono ni marca visible en el historial que diga «viene de un generador».
- **10b Lightbox ✅ (rectangular y redonda):** forma, ancho/alto o diámetro, profundidad, pared, frente (el panel fino que difunde la luz), tapa trasera, labio y holgura de la tapa, agujero del cable. Genera 5–6 operaciones (caja, hueco, agujero del cable, tapa, labio) y dos cuerpos con nombre, **Caja** y **Tapa**, agrupados en el componente **Lightbox**; al cambiar un parámetro (Sólido › Parámetros) se recalcula todo. La tapa sale al lado de la caja para imprimirla. Comprobado en la app (vista previa rectangular y redonda; aceptar crea componente, cuerpos y 5 operaciones).
- **Salida del cable elegible ✅ (pedido en las pruebas):** pregunta «Salida del cable»: atrás (en la tapa, por defecto), pared de abajo, de arriba, izquierda o derecha; el agujero de la tapa atraviesa placa y labio. Todas las medidas y convenciones de los generadores están documentadas en [`generadores.md`](generadores.md). 1 prueba nueva.
- **Lightbox como la de MakerLab ✅ (según las referencias del usuario):** analizados los dos `.3mf` de ejemplo (medidas en [`generadores.md`](generadores.md)). Ahora la caja está **abierta por delante** (fondo cerrado en Z = 0, apoyado en la cama) y la **tapa va en el frente**, hecha de cuatro cuerpos con nombre: **Caja**, **Marco** (anillo de 2 mm y 0,8 de grueso), **Labio** (anillo de 1 mm que entra 5 mm, con holgura) y **Cara** (la zona de la imagen, aparte para darle su filamento o su SVG sin tocar el marco). Esquinas exteriores redondeadas (`BoxFeature.cornerRadius`, también en la herramienta Caja), fondo, frente, labio y esquina como preguntas; valores por defecto de la referencia (175 × 131 × 37, pared 2, frente 0,8). La salida del cable por defecto es el fondo. 3 pruebas cambiadas y 1 nueva.
- **Pendiente de la lightbox:** poner el SVG del usuario sobre la cara central (relieve o corte; hace falta importar SVG como sketch o como piezas); exportar 3MF con varios cuerpos y filamentos por cuerpo (hoy: un archivo por cuerpo); el canal para la tira LED; la forma redonda no se comparó con ninguna referencia.
- **Pendiente de 10a/10b:** el **canal para la tira LED**; una tarjeta «Herramientas» sobre el lienzo vacío (hoy la app abre directo en un lienzo vacío y no hay tarjeta «Diseñar desde cero»); al editar después hay que tocar los parámetros o las operaciones sueltas (no se reabre el formulario del generador); la tapa no lleva muesca para el cable; los generadores trabajan en mm y los cuerpos salen alrededor del origen (si ya hay piezas ahí, el hueco de la caja puede cortarlas); sin miniatura ni dibujo explicativo en el formulario.
- **10c Caja con tapa ✅ (primera parte):** generador «Caja con tapa»: caja rectangular abierta por arriba (ancho, fondo, alto, pared, base, esquinas redondeadas) y tapa al lado, con cierre de **labio a presión** (alto, grosor y holgura) o de **tornillos en las esquinas** (cuatro postes con agujero para roscar y cuatro agujeros pasantes en la tapa, medidas M3 por defecto). Medidas y operaciones en [`generadores.md`](generadores.md). Icono propio por generador. 1 prueba nueva.
- **Pendiente de 10c:** caja redonda, bisagra, ventilación y ranuras, patas, divisiones interiores, soportes y carcasas con aberturas (USB, interruptor); los agujeros de la tapa y de los postes son de medida M3 fija (no hay catálogo de tornillos M2–M6).
- **10d Con texto ✅:** generador «Letrero o llavero»: placa con esquinas redondeadas del tamaño del texto más un margen, con el texto **en relieve** (cuerpo aparte, otro color), **grabado** o **suelto**, y anilla con agujero para llavero. El contorno de las letras lo da la vista (`Viewport::textOutline`, fuentes del sistema; los generadores lo piden con `setTextOutliner`) y se guarda como un sketch editable; el generador descarta las regiones que rellenan los huecos de las letras. Para esto, las recetas pueden incluir un sketch y operaciones que lo usan (`kRecipeSketch`, `instantiate`), y los formularios aceptan texto y fuente (`GeneratorField::Kind::Text/Font`, `GeneratorValues::texts`). Medidas en [`generadores.md`](generadores.md). 1 prueba nueva; comprobado en la app (llavero «Nufio» en Segoe UI).
- **Pendiente de 10d:** varias líneas de texto, alineación y espaciado; texto sobre una curva o un cilindro; cambiar el texto después sin rehacer el diseño (hay que editar el sketch y `placa_ancho`/`placa_alto`); SVG del usuario en lugar de texto (importar SVG como sketch); fuentes con letras que se solapan (usa la unión que ya hace el sketch de texto, no comprobada con todas); imagen o logotipo en relieve; 3MF multicolor.
- **10e Par de engranajes ✅:** generador «Par de engranajes»: dos ruedas (módulo, dientes de cada una, ángulo de presión, ancho, hélice, agujeros y juego) a la distancia entre ejes `módulo × (z1 + z2) / 2 + juego`, la segunda **girada** para que los dientes encajen (parámetro calculado `giro_rueda_2`) y, con hélice, de sentido contrario; el formulario muestra la relación de transmisión, la distancia y los diámetros primitivos. Para esto: el engranaje tiene **Giro inicial** (`GearFeature::rotation`, también en su herramienta), el número de dientes de su herramienta admite expresiones, y los formularios de los generadores aceptan cantidades y ángulos y un resumen (`Generator::summary`). Se comprobó con una booleana que las ruedas no se solapan con el giro y que sí se solapan sin él. Medidas y fórmulas en [`generadores.md`](generadores.md). 3 pruebas nuevas.
- **Corregido de paso:** las expresiones de campos en grados (ángulo de presión, hélice, ángulos de extrusiones…) se convertían dos veces a radianes al calcular; ahora valen lo que dicen (antes un ángulo ligado a un parámetro daba un valor casi nulo).
- **Pendiente de 10e:** el aviso de compatibilidad en el panel del engranaje suelto (módulo, ángulo de presión, hélice) cuando hay otro cerca no se hizo; la forma de las ruedas con pocos dientes (< 10) sale puntiaguda (ver 7a); con hélice no se comprobó el engrane con una booleana (solo se comprueba en dientes rectos); no hay opción de ruedas separadas para imprimir ni de rueda interior/cremallera.
- **Más adelante (aparte):** litofanías y relieves desde imagen (mallas, camino fuera de OpenCASCADE) y letreros multicolor (requiere exportar 3MF).

---

### Prueba automática de la interfaz ✅ (de la segunda auditoría)
`NufioCAD.exe --prueba-interfaz` (`InterfaceCheck`): con la ventana real recorre generadores y herramientas (abrir, cambiar un valor, aceptar, deshacer, cancelar), las unidades, la sección y la medición, y escribe un informe con la parada más larga de la interfaz por paso. Un **vigilante** en otro hilo, si la interfaz lleva más de un segundo sin dar señales, la detiene un instante, copia su pila y la descifra, así que el informe dice **dónde** estaba parada. Aisla ajustes y carpetas (`QStandardPaths::setTestModeEnabled`, ajustes en un `.ini` temporal). Cómo usarla: ver la guía de pruebas.
- **Encontró un fallo real:** al aceptar o cancelar una herramienta con vista previa, la vista previa pendiente (la que espera 250 ms) se calculaba y se dibujaba **después** de cerrarla (hasta 6 s de parada en Debug y un dibujo de más tras aceptar). Ahora `stopPreview()` la cancela al cerrar; las paradas de las herramientas pasaron de 5–6 s a 60–130 ms.
- **Pendiente de la prueba de interfaz:** no comprueba cómo se ve (solo que hay o no cuerpos, errores y paradas); no pulsa con el ratón sino con el código de los botones; solo Windows para el vigilante de pilas (en Linux no vigila); falta añadirla al CI cuando haya una pantalla virtual (Xvfb); los generadores con muchas operaciones en Debug aún paran la interfaz 1–2 s (el Letrero, al calcular las regiones de las letras, y el Par de engranajes).

### Fase de pruebas (manual, transversal)
Guía paso a paso para probar cada fase y dar feedback: [`guia-de-pruebas.md`](guia-de-pruebas.md) (con plantilla para reportar). Se hace fase por fase cuando haya tiempo; lo que salga se anota aquí como pendiente de la fase correspondiente.

**Convención:** cada bloque termina con su línea «**Pendiente de …**» (lo que no se hizo, lo que no se probó a mano, límites conocidos). Todo pendiente nuevo se documenta ahí al cerrar el bloque, y al hacer la segunda auditoría de código se repasan todos.

## Backlog consolidado (revisión del 7 nov. 2026)

Resumen de todo lo pendiente que no son pruebas manuales; el detalle sigue en el «Pendiente de …» de cada bloque. Se trabaja por oleadas y al cerrar cada punto se tacha aquí y en su bloque.

**Oleada 1 — Cimientos (hacer antes de ampliar generadores)**
- [x] Cotas del sketch ligadas a parámetros (6c). Falta probarlo a mano (guía 6.20–6.24).
- [x] Renombrar un parámetro actualiza las expresiones que lo usan (6c). Falta probarlo a mano (guía 6.17–6.19).
- [x] Cantidad del patrón ligable desde la interfaz (6c): el cuadro acepta expresiones y se redondea al entero (mínimo 2). Falta probarlo a mano (guía 5.12–5.14).
- [x] Generadores reabribles (10a): cada operación guarda de qué generador salió y, la primera, las respuestas; clic derecho › «Reabrir…». Falta probarlo a mano (guía G.12–G.16).
- [x] Completar la guía de pruebas: las fases 5–8, 9c, 9d, unidades y Fase 10 ya estaban; se ordenaron (las secciones sueltas tras la plantilla pasaron a su sitio), se quitó lo caducado y se añadieron 5.12–5.14 y G.12–G.16.

**Oleada 2 — Robustez**
- [ ] Compilar y probar en Fedora/CI de Ubuntu (enlace STEP, nlohmann-json, filtro `*.exe`).
- [ ] Prueba de interfaz en el CI (Xvfb) y que compruebe también cómo se ve.
- [ ] Medir en Release (bloqueado por Smart App Control; probar en otra máquina o firmar).
- [x] Partir `FeatureTools.cpp` (en 4 archivos más `FeatureToolsCommon.hpp`) y `Features.cpp` (en 2 más `FeaturesCommon.hpp`): solo se movió código, sin cambiar nada.
- [x] Partir `Viewport.cpp` (queda el widget, ~600 líneas; el `Impl` y los dibujos pasaron a `ViewportImpl.hpp` y `ViewportDrawing.hpp`, privadas) y `MainWindow.cpp` (en `MainWindowMenus.cpp`, `MainWindowView.cpp` y `MainWindowCommon.hpp`). Solo se movió código; pruebas y prueba de interfaz pasan. Pendiente: partir `ViewportImpl.hpp` (1700 líneas) por responsabilidades (sketch, selección, vista previa).
- [x] `hasHeavySurface`/`hasMesh` sin recorrer todas las caras en cada refresco: el resultado se guarda en la propia forma (`Shape::Impl`, inmutable): «pesada» se calcula una vez y «con malla» se recuerda cuando es cierta.
- [ ] Limitaciones de Fase 2 y 3 (ver tabla de Estado): lo proyectado y el texto bloqueados; recortar/extender/desfase solo con líneas, arcos y círculos; elegir qué cuerpos participan en unir/cortar; «hasta una cara» con caras no planas; flecha en la revolución.

**Oleada 3 — Usabilidad**
- [x] Generadores: la ayuda de cada campo sale como tooltip (se probó un texto fijo bajo el campo y no gustó) y las últimas respuestas se recuerdan por generador (`QSettings`, `generator/<id>/<clave>`; se guardan al aceptar un diseño nuevo). Falta probarlo a mano (guía G.17–G.18; la flecha de medida ya lo comprueba la prueba de interfaz). **Pendiente:** agrupar los campos por secciones (medidas, tapa, cable…) con títulos plegables; un botón «Restablecer valores».
- [ ] Vista previa de generadores en color translúcido; indicador «Preparando…» sobre la vista.
- [x] Errores de geometría en lenguaje claro: los fallos del kernel de OpenCASCADE («Error del kernel al extruir: …») ahora dicen qué se intentaba, qué probar y, al final y entre paréntesis, el detalle técnico (`kernelFailure` en `Modeling.cpp`: extruir, revolucionar, combinar cuerpos, reflejar, repetir, engranaje, caja, cilindro, rosca, barrer, loft, mover, colocar, agujero); la booleana que falla explica que puede ser por cuerpos que solo se tocan en una arista o por caras casi coincidentes; el «Error inesperado» del documento sugiere probar otros valores o deshacer. Los mensajes que ya eran claros (validaciones de medidas, redondeo, vaciado) no cambian. **Pendiente:** las sugerencias son fijas por operación (no distinguen la causa concreta que dio el kernel); sin traducir el detalle técnico, que sigue en inglés; las ayudas con «mm» fijo en los textos de los generadores («M3: 2,5») no siguen la unidad elegida.
- [ ] Selector de unidad en la barra de estado; atajos para Caja, Cilindro y generadores.
- [ ] Ocultar la pestaña HERRAMIENTAS dentro de un sketch; aviso de recuperación menos molesto.
- [ ] Unidades restantes: Medir, avisos de impresión, lista de parámetros, área/volumen, por documento.
- [ ] Interfaz Fusion: paleta flotante del sketch, barra superior, menú radial, valores junto al cursor en Extruir/Revolución.

- [x] Flecha de doble punta en la vista para la medida que se edita (pedido del usuario): `FeatureTool::track/dimensionArrow/dimensionArc` + `Viewport::showDimensionArrow/Arc` (flecha recta o arco para ángulos, color turquesa). Hecha para Extruir (distancia, distancia 2, desfase), Caja, Cilindro, Plano (distancia y ángulo), Mover (X/Y/Z y ángulo), Patrón (distancia y ángulo circular), Rosca (diámetro, largo, paso), Engranaje (módulo/dientes, ancho, agujero), Redondeo/Chaflán (tamaño, aristas rectas), Vaciado (espesor), Agujero (diámetro, profundidad, cabeza), la posición X/Y de las colocaciones y los generadores (`Generator::arrows()`: Lightbox, Caja con tapa, Letrero, Par de engranajes, en las medidas que son una pieza). La prueba de interfaz comprueba por píxeles Caja, Cilindro, Rosca, Engranaje, Plano, Mover, Patrón, Lightbox y Caja con tapa. Falta probarla a mano (guía F.1–F.10).
- [x] **Flechas que faltaban ✅:** `document::axisOf` (la recta de un eje, para dibujar alrededor de ella) y `ArrowSpec::at/length` en los generadores (un punto concreto de la pieza y una medida calculada). Nuevas: Revolución (arco de «Ángulo» y «Ángulo 2» alrededor del eje, pasando por el centro del perfil; en simétrica empieza a −ángulo/2), conicidad de Extruir (arco entre la normal y la pared), Unión (distancia y desplazamiento en la cara fija, arco del ángulo alrededor de su normal), Engranaje (arcos del giro inicial, del ángulo de presión en el punto primitivo y del de hélice), radio de esquinas de la Caja, holgura de la Rosca (marca el diámetro del agujero resultante, porque 0,15 mm no se vería), y en generadores: radio de esquinas (Lightbox, Caja con tapa, Letrero), grosor del labio, postes, agujero del poste y de la tapa, grosor del labio de la tapa, margen, anilla y agujero de la anilla del Letrero, módulo y dientes del par (Ø primitivo) y los agujeros del par, que ahora se marcan en el centro de la rueda (antes salían en su borde). La prueba de interfaz encuadra la pieza antes de contar píxeles (una medida pequeña en una vista lejana son pocos píxeles) y guarda una captura en `%TEMP%\flecha-*.png` de cada flecha que falla; suma Caja (radio), Engranaje (presión, giro, hélice) y Par de engranajes (módulo).
- **Pendiente de las flechas:** sin probar a mano ni en la prueba automática: Revolución, conicidad de Extruir, Unión, Rosca (holgura: la fila solo está con «Cortar») y los generadores con la caja con tapa (esquina, postes, agujeros) y el Letrero (anilla); la flecha del ángulo de presión es una guía, no mide sobre el diente real; sin flecha: holgura y agujero del cable de los generadores, ángulo de presión y de hélice del par de engranajes, tamaño del texto del Letrero, ángulo de la cabeza del Agujero. Redondeo, Vaciado y Agujero tampoco están en la prueba automática (necesitan elegir aristas, caras o un sketch).

**Oleada 4 — Ampliaciones de modelado**
- [ ] Repetir y reflejar operaciones (no solo cuerpos); patrón por trayectoria; espejo de varios cuerpos.
- [ ] Redondeo/chaflán: dos distancias, ángulo, radio variable, redondear una cara entera.
- [ ] Vaciado cerrado y espesores por cara; agujeros con punto y clic en cara; roscas sobre caras cilíndricas.
- [ ] Barrido y loft con guías, torsión y tangencias; mover con gizmo; dividir cuerpos; componentes con origen propio; uniones cilíndricas.
- [ ] Medir con línea dibujada y copiar; Sección con caras y planos de construcción; voladizos y soportes en la revisión.
- [ ] STEP: pulgadas, colores, nombres y ensamblajes (XDE); reimportar; guardar la cámara; calidad de malla «Personalizada».

**Oleada 5 — Generadores y salida**
- [ ] SVG como sketch (letrero y lightbox); 3MF multicuerpo con filamentos.
- [ ] Lightbox: canal para tira LED. Caja con tapa: redonda, bisagra, ventilación, patas, divisiones, catálogo de tornillos M2–M6.
- [ ] Letrero: varias líneas, alineación y cambio de texto sin rehacer. Engranajes: internos, cremallera, pocos dientes, aviso de compatibilidad.
- [ ] Tarjeta «Herramientas» sobre el lienzo vacío; control de calidad de malla al exportar.

## Notas de planificación

- **Orden:** la Fase 4 va justo después del modelado básico para tener un flujo útil de punta a punta lo antes posible; cada fase siguiente agrega valor real.
- **Esfuerzo:** las Fases 1–3 son alcanzables en semanas; las Fases 4–5 bien hechas (incluyendo robustez del historial) concentran la mayor parte del esfuerzo.

## Estado

| Fase | Estado |
|---|---|
| Hoja de ruta | ✅ Aprobada |
| Fase 0 | ✅ Completa — mockups, formatos de archivo, arquitectura, entorno (repositorio con CMake, vcpkg, pruebas y CI) y nombre de trabajo NufioCAD |
| Fase 1 | ✅ Implementada y probada en Linux (Fedora) y Windows (MSVC + vcpkg + Qt 6.8.3, build Debug): visor OCCT en Qt, órbita/pan/zoom estilo Fusion, ViewCube, resaltado y selección de caras y aristas, barra de navegación con Orbitar/Desplazar/Zoom |
| Interfaz Fusion | 🚧 Iconos, cinta con pestañas Sólido/Sketch, barra de navegación y tema en caliente hechos (ver sección «Interfaz estilo Fusion 360»). Pendiente: menú radial, valores junto al cursor en Extruir, y verificar en Fedora |
| Windows | 🚧 Compila (MSVC), las pruebas pasan y la app abre y funciona en Debug. Pendiente: pasar las pruebas manuales 1–30 en Windows y el build Release (Smart App Control bloquea las DLL sin firmar de OCCT en esta PC) |
| Fase 2 | 🚧 Implementada (2a–2d) — sketch completo: dibujo con variantes, elipse, spline y cónica; restricciones (incluida G2), cotas y valores junto al cursor; selección (también por rectángulo), deshacer, arrastre y perfiles; edición (recortar, extender, partir, empalme, chaflán, desfase, simetría, patrones, mover/copiar/girar/escalar); proyectar aristas, intersección, texto, imagen de referencia e importar DXF; los sketches terminados se guardan, sus perfiles y curvas se seleccionan en la vista 3D y se reabren con doble clic. Pendiente: las pruebas manuales de abajo y probar en Windows. Limitaciones: lo proyectado y el texto quedan bloqueados (no se actualizan con la pieza ni se editan como texto hasta la Fase 3); recortar/extender/partir y desfase trabajan con líneas, arcos y círculos; los patrones son copias independientes |
| Fase 3 | 🚧 Implementada (3a–3c) — núcleo paramétrico con nombres estables de caras y aristas; sketches en el historial (sobre planos de origen o caras que siguen a la pieza); Extruir (todas sus variantes, empujar una cara) y Revolución con vista previa y flecha; timeline editable (editar, marcador, reordenar, suprimir, borrar, renombrar), navegador y deshacer/rehacer; lo proyectado sigue a su arista. Pendiente: las pruebas manuales 20–30 y probar en Windows. Limitaciones: los cuerpos que participan en unir/cortar son todos los que toca (sin elegirlos); «hasta una cara» solo con caras planas; el ángulo de la revolución se escribe en el panel (sin flecha) |
| Fase 4 | ✅ 4a (Exportar), 4b (serialización), 4c (Nuevo/Abrir/Guardar/Guardar como con aviso de cambios), 4d (importar STEP), 4e (pantalla de inicio con recientes) y 4f (recuperación) implementadas. Pendiente: probar a mano las pruebas 31–38 y compilar en Fedora/CI |
| Fase 5 | ✅ 5a (redondeo y chaflán), 5b (shell), 5c (agujeros), 5d (patrones de cuerpos) y 5e (espejo de cuerpos) implementadas; pendiente repetir/reflejar operaciones |
| Fase 6 | ✅ 6a (planos de construcción), 6b (ejes de construcción) y 6c (parámetros con nombre y expresiones) implementadas. Pendiente: cotas del sketch ligadas a parámetros. Siguiente paso: Fase 7 |
| Fase 7 | ✅ 7a (engranajes), 7b (roscas), 7c (barrido) y 7d (loft) implementadas. Siguiente paso: Fase 8 |
| Fase 8 | ✅ 8a (mover/copiar/combinar cuerpos), 8b (nombres y componentes) y 8c (uniones entre caras planas) implementadas |
| Fase 9 | ✅ 9a (revisión antes de exportar), 9b (Imprimir), 9c (Medir) y 9d (Sección) implementadas; sin calidad de malla ni línea de medida dibujada |
| Fase 10 | ✅ 10a (base), 10b (Lightbox), 10c (Caja con tapa), 10d (Letrero o llavero) y 10e (Par de engranajes) implementadas; ver [`generadores.md`](generadores.md) |
| Pruebas manuales | ⏳ Guía en [`guia-de-pruebas.md`](guia-de-pruebas.md) (fases 1 a 9b); **faltan por añadir a la guía** 9c, 9d, unidades, colocación de roscas/engranajes y la Fase 10. El usuario irá probando fase por fase y dando feedback |
| Auditoría de código | ✅ Segunda auditoría hecha: [`auditoria-2.md`](auditoria-2.md) (hallazgos arreglados, pendientes por prioridad, usabilidad y rendimiento) |
