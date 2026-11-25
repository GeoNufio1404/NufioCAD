# Auditoría 4: rendimiento, fiabilidad y consistencia (25 nov. 2026)

Motivo: el **bote con tapa de rosca** tardaba mucho, decía «Bote: hueco: No corta ningún cuerpo» con los valores de fábrica
y no mostraba la flecha turquesa de la medida que se edita. Se revisó de ahí hacia toda la aplicación.

## Cómo se revisó

- **Generadores:** los 10, con cada opción (83 casos) y con cada medida al 60 % y al 160 % (445 casos): ninguna operación con error
  ni advertencia, cuerpos válidos y de una pieza. Lo primero queda como prueba normal
  (`tests/test_generators_audit.cpp`); los tiempos y las medidas, como pruebas ocultas (`nuf_tests "[.audit]"` y `"[.audit3]"`).
- **Herramientas:** la prueba de interfaz recorre las medidas visibles de 8 herramientas y los 10 generadores (121) y comprueba
  que cada una tenga flecha o arco; y edita sin tocar nada una operación de cada clase de las que sacan los generadores (30) para
  ver que no cambie.
- **A ojo:** capturas del bote (entero y cortado) con `--captura-temas`.

## Hallazgos

| # | Qué pasaba | Gravedad | Arreglo |
|---|---|---|---|
| 1 | `touches` (¿se tocan dos cuerpos?, se llama en **cada operación con cada cuerpo**) tardaba 0,9–1,7 s si había una rosca: era más del 80 % de los 5,3 s del bote | Alta (rendimiento) | Primero mira si hay puntos de uno dentro del otro (barato y casi siempre basta); si no, solo las parejas de caras con cajas que se cruzan: **0–72 ms** |
| 2 | Una herramienta metida **entera dentro** de un cuerpo «no lo tocaba»: un hueco cerrado no se cortaba. El bote de rosca salía **macizo** con un aviso | Alta (fiabilidad) | La misma función ve la contención; vale también para Unir |
| 3 | Las pruebas del bote daban por buenas las **advertencias** y solo pedían un volumen mínimo (que un bote macizo cumple) | Alta (pruebas) | La prueba nueva exige cero advertencias en todos los generadores y comprueba que el bote esté hueco |
| 4 | Al **editar** una operación ligada a parámetros, sus cuadros perdían la expresión (Qt pide el texto del mínimo y el máximo para calcular el tamaño, y eso la borraba): aceptar la desligaba sin avisar | Alta (fiabilidad) | `ExpressionSpinBox` solo olvida la expresión cuando de verdad cambia el valor |
| 5 | Editar un cilindro generado le quitaba el **redondeo del fondo**; editar una rosca le quitaba la **posición Z** | Alta (fiabilidad) | Los campos salen en el panel (Redondeo de la base, Chaflán de arriba, Posición Z) y lo que un panel no muestra se conserva |
| 6 | El cuello del bote de rosca colgaba de un anillo de 0,56 mm (con paredes finas, de nada) | Alta (pieza) | Hombro interior a 45° hasta la boca; el cuello es 0,6 mm más delgado que el cuerpo |
| 7 | El chaflán de entrada de las roscas se hacía con una booleana: 623 ms frente a 6 ms de la rosca | Media (rendimiento) | La propia superficie se estrecha en el extremo |
| 8 | El bote no tenía **ninguna** flecha; faltaban en bisagra, patas, clips, cable, canal LED, texto del letrero, planetario, cónicos, sin fin, cremallera, aro, surco, chaflán, Dividir, Agujero sobre una cara y el segundo radio de Redondeo | Media (consistencia) | Todas puestas; las de la Caja con tapa ya no se corren con las patas, los clips o la bisagra |
| 9 | Pasar directamente de una herramienta a otra: la vieja se destruía después de abrir la nueva y le quitaba las flechas y la ayuda de aristas (Redondeo → Chaflán) | Media (consistencia) | `FeatureTool::leaveViewport` al cerrar |
| 10 | El nombre libre de los parámetros se buscaba en el hilo de la interfaz armando la receta: lento en el Letrero, y con medidas imposibles la excepción salía en ese hilo | Media (fiabilidad) | Se busca en el hilo de trabajo, que explica el problema |
| 11 | Al aceptar con las respuestas de la vista previa se perdían los **cuerpos fijos** (la corona del tren planetario) | Media (fiabilidad) | Se copian con la receta |
| 12 | Bote con medidas imposibles (pines que no caben, cuello corto, guía que atraviesa la pared): salía un sólido válido que no cierra | Media (fiabilidad) | Avisos con palabras que dicen qué cambiar |
| 13 | «Ranuras por pared» (una cantidad) se mostraba en milímetros | Baja (consistencia) | Es una cantidad |

## Antes y después (Release, este equipo)

| Caso | Antes | Después |
|---|---|---|
| Bote de rosca, todo el cálculo | 5 316 ms (y macizo) | 830 ms |
| Vista previa del bote en la aplicación | — | 855 ms |
| Aceptar el bote (cálculo en segundo plano) | — | 758 ms |
| `touches` rosca con cilindro | 909–1 759 ms | 0–72 ms |
| Rosca con chaflán de entrada | 623 ms | 5 ms |

## Pendiente de la auditoría 4

- Aceptar un generador vuelve a calcular lo que la vista previa ya calculó (M1): ~0,8 s en el bote, en segundo plano.
- Sin flecha: redondear una arista curva; los ángulos de los generadores; las holguras de décimas (juego, adelgazar el diente).
- La cara del chaflán de entrada de una rosca ya no es una cara aparte: un diseño antiguo que la citara dirá que no existe.
- Las flechas turquesa de los generadores nuevos se comprobaron por coordenadas (a menos de 3 mm de la pieza) y dos a ojo; falta la
  prueba manual (REN.1–REN.8).
- «Cada medida tiene su flecha» no recorre las herramientas que necesitan elegir algo (Extruir, Revolución, Agujero, Unión).
