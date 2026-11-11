# LoRa Rangers build helpers.
# Requires `just` and `arduino-cli` on your PATH.
#   just setup                  install the ESP32 core and all libraries
#   just compile DHT11Sensor    compile one sketch in modules/
#   just fmt                    format all code (needs clang-format)
#   just log /dev/ttyUSB0       record base station packets to packets.csv (needs uv)

[windows]
set shell := ["powershell.exe", "-NoLogo", "-Command"]

esp32_url := "https://espressif.github.io/arduino-esp32/package_esp32_index.json"

# Board to build for (override with: just fqbn=esp32:esp32:<board> compile ...)
fqbn := "esp32:esp32:esp32"

# List available recipes
default:
    @just --list

# Install the ESP32 core and every library the project uses
setup: install-core install-libs link-libs

# Install the ESP32 Arduino core
install-core:
    arduino-cli core update-index --additional-urls {{ esp32_url }}
    arduino-cli core install esp32:esp32 --additional-urls {{ esp32_url }}

# Install the Arduino libraries used by the modules
install-libs:
    arduino-cli lib update-index
    arduino-cli lib install "DHT sensor library" "Adafruit Unified Sensor" "TinyGPSPlus" "RadioHead"

# Link each modules/ folder into the Arduino libraries folder, so sketches can
# `#include <PacketCodec.h>` etc. Links point at this repo, so edits apply instantly.
[unix]
link-libs:
    #!/usr/bin/env sh
    set -eu
    libs="$(arduino-cli config get directories.user)/libraries"
    mkdir -p "$libs"
    for d in modules/*/; do
        name=$(basename "$d")
        ln -sfn "$PWD/modules/$name" "$libs/$name"
        echo "linked $name"
    done

# Windows uses directory junctions (no admin rights needed); existing entries are left alone
[windows]
link-libs:
    $libs = Join-Path (arduino-cli config get directories.user) 'libraries'; New-Item -ItemType Directory -Force $libs | Out-Null; Get-ChildItem modules -Directory | ForEach-Object { $t = Join-Path $libs $_.Name; if (Test-Path $t) { "skipped $($_.Name) (already exists)" } else { New-Item -ItemType Junction -Path $t -Target $_.FullName | Out-Null; "linked $($_.Name)" } }

# Compile one sketch folder in modules/ (e.g. just compile DHT11Sensor)
compile sketch:
    arduino-cli compile --fqbn {{ fqbn }} modules/{{ sketch }}

# Record packets from the base station into a CSV on this PC (needs uv), e.g.
#   just log /dev/ttyUSB0       just log COM3 run1.csv
log port out="packets.csv":
    uv run tools/serial_to_csv.py {{ port }} --out {{ out }}

# Generate compile_commands.json for clangd in every sketch (rerun after adding files/libraries)
lsp: (compiledb "DHT11Sensor") (compiledb "GPS") (compiledb "LoRaTx") (compiledb "PacketCodec") (compiledb "SerialForwarder")

# Generate modules/<sketch>/build/compile_commands.json for one sketch
compiledb sketch:
    -arduino-cli compile --fqbn {{ fqbn }} --only-compilation-database --build-path modules/{{ sketch }}/build modules/{{ sketch }}

[unix]
fmt:
    clang-format -i $(git ls-files --cached --others --exclude-standard '*.cpp' '*.h')

[windows]
fmt:
    clang-format -i (git ls-files --cached --others --exclude-standard '*.cpp' '*.h')
