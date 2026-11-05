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
- **Pendiente de 6c:** cotas del sketch ligadas a parámetros; renombrar un parámetro no actualiza las expresiones que lo usan; cantidad del patrón solo ligable desde el archivo (el cuadro es entero).

### Fase 7 — Generadores
- **7a Engranajes ✅:** `geometry::gear` (perfil de involuta con splines por flanco, cabeza y raíz en arcos, tramo radial si la base queda sobre la raíz; recto por prisma, helicoidal por secciones giradas en un solo sólido; agujero central) y `GearFeature` (plano de origen o de construcción, módulo, dientes 6–300, ángulo de presión 5–35°, ancho, agujero, ángulo de hélice hasta 45°, operación nuevo cuerpo/unir; todos ligables a parámetros). Caras con nombre estable: `f<id>:lado:d<i>:flanco_d|flanco_i|cabeza|raiz|rad_d|rad_i`, `…:lado:agujero`, `…:inicio`, `…:fin`. Herramienta Sólido › Engranaje (grupo GENERAR). Errores claros (dientes puntiagudos o solapados, agujero demasiado grande). 3 pruebas nuevas.
- **Pendiente de 7a:** engranajes internos y cremallera, redondeo de raíz, perfil de cicloide, juego/corrección de dentado.
- **7b Roscas ✅:** `geometry::thread` (varilla roscada métrica ISO aproximada: cresta P/8, fondo P/4, profundidad 0,6134 P; derecha o izquierda; el corte transversal es una curva cerrada que se gira a lo largo del eje y se une con superficies regladas, 16 secciones por vuelta) y `ThreadFeature` (plano, diámetro, paso, largo, operación nuevo cuerpo/unir/cortar; con «Cortar» sobre un agujero del diámetro menor da una rosca interior; ligable a parámetros). Herramienta Sólido › Rosca con medidas estándar M3–M24 o propias; avisa del diámetro menor para el agujero. Caras `f<id>:rosca`, `:inicio`, `:fin`. 2 pruebas nuevas.
- **Pendiente de 7b:** el cálculo es lento en Debug (unos 2 s por rosca de 10 mm; la vista previa de una de 20 mm tarda ~10 s) — medir en Release; rosca sobre una cara cilíndrica existente, perfiles no métricos (trapezoidal, UNC) y chaflán de entrada.
- **7c Barrido (sweep) ✅:** `geometry::sweep` (contornos del perfil con `BRepOffsetAPI_MakePipeShell`; trayectoria = curvas de otro sketch que formen un camino continuo; esquinas redondeadas; orientación que sigue la trayectoria o fija; huecos del perfil restados) y `SweepFeature` (perfiles + sketch y curvas de la trayectoria + operación). Caras `f<id>:lado:<origen>`, `:inicio:<perfil>`, `:fin:<perfil>`. Herramienta Sólido › Barrido (grupo CREAR): se eligen en la vista las regiones del perfil y las curvas de la trayectoria. Errores claros si el camino está roto. 2 pruebas nuevas (recto, en L, camino roto, guardar).
- **Pendiente de 7c:** guías/secciones intermedias, torsión y escala a lo largo del camino, trayectoria con aristas de un cuerpo; probado a mano solo que el botón aparece (el flujo completo con dos sketches está cubierto por las pruebas).
- Loft: pendiente (7d).

### Fase 8 — Multi-cuerpo y ensambles
- Varios cuerpos en un mismo diseño.
- Componentes.
- Uniones básicas (joints).

### Fase 9 — Pulido para impresión 3D
- Herramientas de medición.
- Análisis de sección.
- Control de calidad de malla al exportar.
- Posible envío directo al slicer.

---

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
| Fase 4 | 🚧 4a (Exportar), 4b (serialización), 4c (Nuevo/Abrir/Guardar/Guardar como con aviso de cambios), 4d (importar STEP), 4e (pantalla de inicio con recientes) y 4f (recuperación) implementadas: **Fase 4 completa**. Pendiente: probar a mano las pruebas 31–38 y compilar en Fedora/CI |
| Fase 5 | 🚧 5a (redondeo y chaflán), 5b (shell), 5c (agujeros), 5d (patrones de cuerpos) y 5e (espejo de cuerpos) implementadas: **Fase 5 completa** salvo repetir/reflejar operaciones. Siguiente paso: Fase 6 |
| Fase 6 | ✅ 6a (planos de construcción), 6b (ejes de construcción) y 6c (parámetros con nombre y expresiones) implementadas. Pendiente: cotas del sketch ligadas a parámetros. Siguiente paso: Fase 7 |
| Fases 7–9 | Pendientes (generadores, ensambles, medición) |
