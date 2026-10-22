# Silver Core — MuWorldEditor

> **Descarga de fuentes:** [MuWorldEditor-sources.zip](MuWorldEditor-sources.zip). Esta entrega publica el código dentro del archivo ZIP; las carpetas descritas debajo están dentro de él.


Código fuente del módulo WorldEditor desarrollado en Silver Core para el editor integrado en el cliente MU clásico.

Copyright (c) 2026 Julian Di Sanzo. Los avisos y licencias de terceros se conservan en sus archivos.

## Alcance de esta publicación

Esta primera publicación contiene una instantánea del módulo `src/MuEditor/WorldEditor` y las fuentes de sus pruebas en `tests/world_editor`. Los archivos copiados se identifican por tamaño y SHA256 en `SOURCE-MANIFEST.json`.

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

La publicación pública de estas fuentes fue autorizada por el titular. No se ha elegido una licencia de código abierto para los aportes propios: se conserva `LICENSE-SILVER-CORE.txt`. Esta publicación concreta no autoriza la publicación de otros componentes de Silver Core ni concede permisos adicionales de reutilización.

ImGuizmo conserva su licencia MIT en `src/MuEditor/WorldEditor/third_party/ImGuizmo/LICENSE`. Las menciones a otras dependencias en `sources.lock.json` identifican versiones; no implican que todas ellas estén incluidas en este repositorio. Las licencias de terceros se aplican a sus respectivos archivos.

