# LoRa Rangers build helpers.
# Requires `just` and `arduino-cli` on your PATH.
#   just setup                  install the ESP32 core and all libraries
#   just compile DHT11Sensor    compile one sketch in modules/

[windows]
set shell := ["powershell.exe", "-NoLogo", "-Command"]

esp32_url := "https://espressif.github.io/arduino-esp32/package_esp32_index.json"

# Board to build for (override with: just fqbn=esp32:esp32:<board> compile ...)
fqbn := "esp32:esp32:esp32"

# List available recipes
default:
    @just --list

# Install the ESP32 core and every library the project uses
setup: install-core install-libs

# Install the ESP32 Arduino core
install-core:
    arduino-cli core update-index --additional-urls {{ esp32_url }}
    arduino-cli core install esp32:esp32 --additional-urls {{ esp32_url }}

# Install the Arduino libraries used by the modules
install-libs:
    arduino-cli lib update-index
    arduino-cli lib install "DHT sensor library" "Adafruit Unified Sensor" "TinyGPSPlus" "RadioHead"

# Compile one sketch folder in modules/ (e.g. just compile DHT11Sensor)
compile sketch:
    arduino-cli compile --fqbn {{ fqbn }} modules/{{ sketch }}

# Generate compile_commands.json for clangd in every sketch (rerun after adding files/libraries)
lsp: (compiledb "DHT11Sensor") (compiledb "GPS") (compiledb "LoRaTx") (compiledb "PacketCodec")

# Generate modules/<sketch>/build/compile_commands.json for one sketch
compiledb sketch:
    -arduino-cli compile --fqbn {{ fqbn }} --only-compilation-database --build-path modules/{{ sketch }}/build modules/{{ sketch }}
