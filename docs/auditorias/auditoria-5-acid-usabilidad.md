# Auditoría 5: rendimiento, usabilidad, ACID y documentación (25 nov. 2026)

La última antes de la v0.2.1. Se midió o se probó cada punto; lo que no, está dicho al final.

## 1. Rendimiento

| Qué | Antes | Después |
|---|---|---|
| Parada más larga de la interfaz (prueba completa, Release) | 501–1 502 ms | 117–208 ms (cuatro pasadas) |
| Agarrar una flecha al colocar un engranaje | 270 ms con una caja como sombra; 1 470 ms con su forma | 31 ms, con su forma |
| Agarrar una flecha en Mover | 335–957 ms | 24 ms |

- **Herramientas cerradas que seguían escuchando al visor.** Hasta destruirse, agarrar una flecha de la herramienta siguiente las
  despertaba: cada una redibujaba sus cuerpos y su sombra. Ahora se desconectan al cerrarse (`FeatureTool::leaveViewport`). Lo
  trajo la sombra de colocación (24–25 nov.).
- **La sombra rehacía la malla** en el hilo de la interfaz. Usa la que ya preparó el visor.
- **Revisado y bien:** booleana sobre 106 caras, 35–39 ms; `touches`, 1,8 ms; historial de 82 operaciones, 1,5 s; sketch de 100
  rectángulos, 14 ms; engranaje de 40 dientes, 86 ms (helicoidal, 444 ms, en segundo plano).

## 2. ACID

`tests/test_acid.cpp` (8 pruebas) y un paso de la prueba de interfaz.

| Propiedad | Qué fallaba | Ahora |
|---|---|---|
| **Atomicidad** | Una orden compuesta (borrar o mover varias, aplicar un generador) que fallaba a medias dejaba el diseño a medias; si fallaba al deshacerse, se perdía de la pila | Todo o nada; la orden sigue en la pila |
| | Exportar encima de un archivo lo vaciaba antes de empezar: si fallaba, se perdía | Se escribe aparte y lo reemplaza al final |
| | Una excepción en una acción cerraba el programa | Se avisa y se sigue, con el diseño como estaba |
| **Consistencia** | No se comprobaba el archivo nuevo antes de reemplazar el bueno, ni el que se abre | `quick_check` y recuento al guardar; `quick_check` al abrir |
| | Un dato cambiado que SQLite y el JSON dan por bueno (un dígito) pasaba inadvertido | Huella del contenido: se abre y se avisa |
| | La 0.2.0 abriría un tubo como un cilindro macizo, sin decirlo | Esos archivos llevan la versión 2 y la 0.2.0 los rechaza con un mensaje |
| | Guardar nada más aceptar un generador perdía los nombres de los cuerpos | Se espera el resultado |
| **Aislamiento** | Dos ventanas con el mismo archivo: la última en guardar borraba lo de la otra | Se detecta (por contenido, no solo por fecha) y se pregunta |
| | Las dos usaban el mismo temporal | Temporal propio por proceso; probado con dos que guardan y uno que lee a la vez |
| **Durabilidad** | El reemplazo no esperaba al disco | `MOVEFILE_WRITE_THROUGH` (Windows), `fsync` de la carpeta (Linux) |
| | Abrir (o ver la miniatura) abría el archivo para escribir | Solo lectura |

La prueba más dura: 64 archivos estropeados (cortados, vacíos, con bytes pisados). Cada uno da un error claro sin tocar el diseño
abierto, o se abre entero y con aviso si algo cambió.

## 3. Usabilidad (heurísticas de Nielsen)

Se añadió un paso a la prueba de interfaz que recorre menús, atajos y botones.

| Heurística | Qué se revisó | Resultado |
|---|---|---|
| 1. Visibilidad del estado | «Calculando…», insignia de ocupado, flecha de la medida, sombra al colocar | La sombra del engranaje es su forma, no una caja |
| 2. Lenguaje del usuario | Mensajes del kernel y de los generadores | Bien; el bote explica las medidas imposibles |
| 3. Control y libertad | Deshacer, cancelar, Esc | Deshacer no se pierde aunque falle |
| 4. Consistencia | Atajos, letras de acceso, flechas de las medidas | 13 letras repetidas dentro de un menú, corregidas; sin atajos repetidos; todas las medidas revisadas tienen flecha |
| 5. Prevenir errores | Pisar el archivo de otra ventana, medidas imposibles | Se pregunta antes de sobrescribir |
| 6. Reconocer antes que recordar | Ayuda de cada acción | 5 acciones sin ayuda, corregidas |
| 7. Eficiencia | Atajos, Alt + letra | Menús manejables con el teclado sin choques |
| 8. Diseño mínimo | Largo de los menús | Sketch tenía 28 entradas: cuatro submenús |
| 9. Recuperarse de errores | Errores inesperados, recuperación | Un error en una acción ya no cierra el programa |
| 10. Ayuda | Tooltips y guía | Opciones de línea de comandos que faltaban |

Botones: ninguno visible de menos de 20 px ni sin nombre.

## 4. Documentación

- 12 códigos de prueba repetidos (`GEN.1`–`GEN.12`) ahora son únicos; las pruebas nuevas tienen su sección (1.10).
- Un enlace roto, el estado de la hoja de ruta y la versión, al día; `auditoria-2.md` no estaba enlazada.
- `--inventario`, `--tamano` y `--escala` no estaban en `desarrollo.md`.
- `arquitectura.md` describe el guardado tal como es ahora.
- `distribucion.md` avisa de que empujar una etiqueta `v*` dispara el flujo Publicar.

## Lo que no se hizo

- Aceptar un generador vuelve a calcular lo que la vista previa ya calculó (M1).
- La huella no cubre los sólidos importados ni la miniatura; abrir un archivo que ya está abierto en otra ventana no avisa hasta guardar.
- La usabilidad se revisó con reglas y leyendo el código, no con personas. La prueba no mira contraste ni lectores de pantalla.
- Un corte de luz de verdad no se probó; sí los restos que dejaría. En Linux no se compiló ni se midió nada de esto.
- Las pruebas manuales ACI.1–ACI.6.
