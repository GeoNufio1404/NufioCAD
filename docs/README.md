# Documentación de NufioCAD

## Qué leer según lo que buscas

| Si quieres… | Lee |
|---|---|
| Entender cómo está hecho el programa (módulos, datos, hilos, archivo) | [`arquitectura.md`](arquitectura.md) |
| Saber por qué se eligió cada tecnología y qué se descartó | [`decisiones.md`](decisiones.md) |
| Compilar, probar, opciones de línea de comandos, ramas | [`desarrollo.md`](desarrollo.md) |
| Ver qué está hecho, qué falta y el plan | [`hoja-de-ruta.md`](hoja-de-ruta.md) |
| Saber qué cambió en cada entrega y cómo deshacerlo | [`registro-de-cambios.md`](registro-de-cambios.md) |
| Probar la aplicación a mano, sección por sección | [`guia-de-pruebas.md`](guia-de-pruebas.md) |
| Las medidas y convenciones de cada generador (Lightbox, Caja con tapa…) | [`generadores.md`](generadores.md) |
| Cifras de rendimiento y cómo medir | [`rendimiento/mediciones.md`](rendimiento/mediciones.md) |
| Revisiones de código y sus hallazgos | [`auditorias/`](auditorias/) |

## Orden recomendado para alguien nuevo

1. El [README del proyecto](../README.md): qué es y con qué está hecho.
2. [`arquitectura.md`](arquitectura.md) §1–§3: los módulos y el modelo del documento.
3. [`desarrollo.md`](desarrollo.md): compilar y pasar las pruebas.
4. [`hoja-de-ruta.md`](hoja-de-ruta.md), secciones «Dónde estamos» y «Lo que sigue, en orden»: qué está hecho y qué viene, por fases.

## Contenido de la carpeta

```
docs/
├── README.md                  este índice
├── arquitectura.md            cómo está hecho (con diagramas)
├── decisiones.md              qué se usó, por qué y qué se descartó
├── desarrollo.md              compilar, probar, herramientas, ramas
├── hoja-de-ruta.md            fases hechas y por hacer, estado y orden
├── registro-de-cambios.md     entregas, archivos tocados y cómo volver atrás
├── guia-de-pruebas.md         pruebas manuales con IDs (SKC.12, ACA.6…)
├── generadores.md             medidas de cada generador
├── rendimiento/
│   └── mediciones.md          tiempos medidos (Debug y Release, Fedora y Windows)
└── auditorias/
    ├── auditoria-2.md         segunda auditoría (6 nov. 2026)
    └── auditoria-3-fluidez.md tercera auditoría: que la ventana no se congele (16 nov. 2026)
```

## Convenciones

- Todo en español; el código y los nombres de archivo también.
- Diagramas en [Mermaid](https://mermaid.js.org) dentro de los `.md` (no imágenes sueltas), para que se puedan editar.
- Cada bloque de la hoja de ruta termina con su línea **«Pendiente de …»**; cada prueba manual tiene un ID único.
- Las rutas de código se citan relativas a `src/` (`document/src/Document.cpp`) o a la raíz del repositorio.
