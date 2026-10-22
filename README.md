# Silver Core — MuWorldEditor

**Desarrollado por Julian Di Sanzo — Silver Core.**

Código fuente del módulo WorldEditor desarrollado en Silver Core para el editor integrado en el cliente MU clásico.

Un aporte a la comunidad para que otras personas puedan estudiar el editor, continuarlo, corregirlo y compartir sus mejoras.

Copyright (c) 2026 Julian Di Sanzo. Aportes propios publicados bajo licencia MIT. Los avisos y licencias de terceros se conservan en sus archivos.

## Alcance de esta publicación

El repositorio independiente contiene las fuentes navegables del módulo [`src/MuEditor/WorldEditor`](src/MuEditor/WorldEditor) y de sus pruebas en [`tests/world_editor`](tests/world_editor). También se ofrece una [descarga de las fuentes en ZIP](MuWorldEditor-sources.zip). Los archivos originales se identifican por tamaño y SHA256 en `SOURCE-MANIFEST.json`.

La instantánea procede de la copia de recuperación del 20/10/2026. No representa una nueva versión compilada ni un historial completo de desarrollo.

Incluye código para documentos de terreno y objetos, selección e historial, capas y prefabs, regiones y triggers, catálogos de modelos y NPC, validación y candidatos, interfaz ImGui y adaptación al cliente. La presencia del código no certifica cada función en ejecución.

## Estructura

- `src/MuEditor/WorldEditor/core`: documentos, formatos y lógica del editor.
- `src/MuEditor/WorldEditor/ui`: interfaz del editor.
- `src/MuEditor/WorldEditor/client`: integración con el cliente original.
- `src/MuEditor/WorldEditor/third_party`: ImGuizmo y registro de dependencias.
- `tests/world_editor`: fuentes y configuración original de pruebas.

## Estado de compilación

Este repositorio no es una aplicación autónoma ni contiene un ejecutable listo para usar. La integración requiere partes del cliente original que no se publican aquí. Las pruebas originales también hacen referencia a dependencias y fixtures externos, como ImGui, OpenSSL, JSON y archivos del juego. Su CMake se conserva como referencia y no debe interpretarse como una compilación independiente lista para ejecutar.

No se realizó una nueva compilación ni ejecución de pruebas para esta publicación. La comprobación realizada es de alcance, archivos y hashes; no acredita funcionamiento, rendimiento ni acabado visual.

## Contenido excluido

Esta publicación no incluye Unreal Engine, proyectos ni derivados de Unreal, el cliente completo MuMain, el servidor, datos del juego, mapas, modelos, texturas, bases de datos, credenciales, configuraciones personales, herramientas operativas privadas, binarios, logs ni respaldos completos.

## Derechos y dependencias

El desarrollo y los aportes propios del editor Silver Core se atribuyen a **Julian Di Sanzo**. Consulta también [`AUTHORS.md`](AUTHORS.md). Esta atribución no reclama la autoría de MuMain, OpenMU, MU Online ni de bibliotecas o aportes de terceros.

Los aportes propios de Julian Di Sanzo incluidos en esta publicación se ofrecen bajo la licencia MIT de `LICENSE`. Se permite usarlos, modificarlos, distribuirlos y continuar su desarrollo, incluso con fines comerciales, conservando el aviso de copyright y el permiso de la licencia. No se exige que las modificaciones se hagan públicas.

Esta autorización se limita al contenido publicado en este repositorio. No cambia la privacidad ni la licencia de otros componentes de Silver Core, de Unreal Engine ni de archivos que no se incluyen aquí. Tampoco concede derechos sobre código o recursos de terceros.

ImGuizmo conserva su licencia MIT y atribución originales en `src/MuEditor/WorldEditor/third_party/ImGuizmo/LICENSE`. Las menciones a otras dependencias en `sources.lock.json` identifican versiones; no implican que todas ellas estén incluidas en este repositorio. Las licencias de terceros se aplican a sus respectivos archivos.

## Continuar el proyecto

Los reportes de errores, documentación y propuestas de mejora son bienvenidos mediante issues y pull requests. Indica el problema que resuelve cada cambio, sus dependencias y cómo lo comprobaste. No adjuntes credenciales, datos personales ni assets del juego que no tengas permiso para compartir.
