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
- Herramienta **Revolución**: perfil + eje, con arrastre del ángulo.
- Deshacer/rehacer de todo el modelado (Ctrl+Z / Ctrl+Y).
- La pieza de demostración desaparece: se empieza con el documento vacío.

**3c — Timeline editable y reconstrucción**
- Editar cualquier operación pasada: se recalcula lo que sigue y las referencias (caras de sketches, «hasta cara») se resuelven por nombre.
- Arrastrar operaciones para reordenarlas (si las dependencias lo permiten) y arrastrar el marcador para volver atrás en el historial (*rollback*).
- Suprimir/reactivar y borrar (avisando si otras dependen de ella).
- Lo proyectado en un sketch queda vinculado a la arista original y se actualiza con la pieza.
- Iconos de error/advertencia en el timeline y el navegador, con el motivo.

### Fase 4 — Archivos y exportación
- Formato nativo `.nuf` (offline y privado), guardando el historial.
- Pantalla de inicio con diseños recientes.
- Exportación a STL, OBJ y STEP (con control de calidad de malla).
- Importación de STEP.

> Hito: desde aquí ya es posible diseñar una pieza simple, exportarla e imprimirla.

### Fase 5 — Operaciones de acabado
- Chaflán y fillet.
- Shell (vaciado).
- Agujeros (con avellanado/abocardado).
- Patrones lineales y circulares.
- Espejo.

### Fase 6 — Geometría de referencia y parámetros
- Planos y ejes de construcción.
- Tabla de parámetros con nombre (ej. `grosor = 3 mm`).
- Expresiones entre parámetros.

### Fase 7 — Generadores
- Engranajes rectos y helicoidales (perfil de involuta).
- Roscas y tornillos métricos (sweep helicoidal).
- Sweep y loft.

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
| Fase 1 | 🚧 Implementada y probada en Linux (Fedora): visor OCCT en Qt, órbita/pan/zoom estilo Fusion, ViewCube, resaltado y selección de caras y aristas. Falta probarla en Windows |
| Fase 2 | 🚧 Implementada (2a–2d) — sketch completo: dibujo con variantes, elipse, spline y cónica; restricciones (incluida G2), cotas y valores junto al cursor; selección (también por rectángulo), deshacer, arrastre y perfiles; edición (recortar, extender, partir, empalme, chaflán, desfase, simetría, patrones, mover/copiar/girar/escalar); proyectar aristas, intersección, texto, imagen de referencia e importar DXF; los sketches terminados se guardan, sus perfiles y curvas se seleccionan en la vista 3D y se reabren con doble clic. Pendiente: las pruebas manuales de abajo y probar en Windows. Limitaciones: lo proyectado y el texto quedan bloqueados (no se actualizan con la pieza ni se editan como texto hasta la Fase 3); recortar/extender/partir y desfase trabajan con líneas, arcos y círculos; los patrones son copias independientes; las curvas que se cruzan sin compartir extremo no parten los perfiles |
| Fase 3 | 🚧 En curso (3a) |
| Fases 4–9 | Pendientes |
