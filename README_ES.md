# chiaki-ng en español y con temas PlayStation

La interfaz incluye 710 traducciones al español. En **Ajustes → General → Tema**
puedes elegir PS5 clásico, PS4, God of War, Spider-Man, Horizon, The Last of Us,
Ghost of Tsushima, Astro Bot, Gran Turismo, Ratchet & Clank, Returnal, Uncharted,
Bloodborne, Demon's Souls, Death Stranding o el estilo original: 16 estilos en total.
El cambio se aplica al instante, el fondo se muestra junto al selector y la elección
se guarda para el siguiente inicio. Los motivos vectoriales son ilustraciones
propias inspiradas en cada juego y solo aparecen en el menú de consolas.

## Abrir en Arch Linux

Desde esta carpeta:

```bash
./scripts/iniciar-es.sh
```

Esta compilación nativa utiliza la configuración nativa de Chiaki. La configuración
de la aplicación Flatpak se guarda por separado; el lanzador no la migra.
La vinculación de la consola sigue requiriendo el Account-ID de PSN y un PIN válido.

## Compilar

Con las dependencias de desarrollo de chiaki-ng y Qt 6 Linguist Tools instaladas:

```bash
cmake -S . -B build-es/cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHIAKI_USE_SYSTEM_CURL=ON \
  -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-es/cmake --parallel 6
ctest --test-dir build-es/cmake --output-on-failure
mkdir -p build-es/bin build-es/lib
cp build-es/cmake/gui/chiaki build-es/bin/
cp build-es/cmake/third-party/cpp-steam-tools/libcpp-steam-tools.so build-es/lib/
./scripts/iniciar-es.sh
```

La compilación depende de las versiones locales de Qt y las bibliotecas multimedia.
Recompila después de actualizarlas. La traducción también puede seguir el idioma del
sistema al abrir el binario directamente; el lanzador selecciona español por defecto.
