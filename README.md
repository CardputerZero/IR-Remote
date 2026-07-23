# CardputerZero IR Remote

IR remote capture and replay app for M5Stack CardputerZero.

## Features

- Capture raw IR signals from the CardputerZero receiver
- Confirm, rename, or discard a captured signal before saving
- Browse and delete saved `.ir` signal files
- Inspect saved signals with waveform, timing metadata, and protocol decoding
- Replay saved signals through the CardputerZero transmitter
- Use generated dummy signals in SDL desktop builds for UI testing

## Dependencies

Run the bootstrap script once after cloning this repository:

```bash
./bootstrap.sh
```

It creates `.venv/`, installs the Python build tools, fetches dependencies from
`repos.json`, and keeps all third-party source under `dependencies/`.

System packages and runtime pieces expected by the app:

- CMake and a C/C++ compiler
- SDL2 development files for `IR_REMOTE_USE_SDL=ON`
- Linux LIRC UAPI headers for device builds (`linux/lirc.h`, usually from `linux-libc-dev`)
- Working rc-core/LIRC device nodes for capture and replay, such as `/dev/lirc0` and `/dev/lirc1`
- `python3-venv` so `bootstrap.sh` can create `.venv/`
- `aarch64-linux-gnu-gcc/g++` for cross-building the CardputerZero package

The device implementation uses the Linux kernel LIRC character-device API
directly with `read`, `write`, and `ioctl`; it does not link against the
`liblirc` userspace client library.

On macOS, install the desktop build tools with Homebrew:

```bash
brew install cmake pkg-config sdl2
```

macOS supports the SDL desktop build only. Device framebuffer, LIRC hardware
access, and Debian packaging are Linux/CardputerZero targets.

Project dependencies pulled from `repos.json`:

- `lvgl`
- `spdlog`
- `smooth_ui_toolkit`

Image asset conversion runs during build and uses LVGL's Python converter. If
the converter dependencies are missing, install them in your Python environment:

```bash
./.venv/bin/python -m pip install pypng lz4 Pillow
```

Font conversion is not part of the normal build. If fonts are regenerated with
`src/assets/convert_fonts.py`, install `lv_font_conv` separately and keep it
available in `PATH`.

## Build

For Linux SDL testing:

```bash
cmake -S . -B build/sdl -DIR_REMOTE_USE_SDL=ON
cmake --build build/sdl -j8
```

For macOS SDL testing:

```bash
cmake -S . -B build/macos-sdl -DIR_REMOTE_USE_SDL=ON
cmake --build build/macos-sdl -j8
```

For CardputerZero framebuffer/LIRC build:

```bash
cmake -S . -B build/cp0 -DIR_REMOTE_USE_SDL=OFF
cmake --build build/cp0 -j8
```

For cross build from x86 Linux with the GNU aarch64 toolchain:

```bash
cmake -S . -B build/cp0 \
  -DIR_REMOTE_USE_SDL=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake --build build/cp0 -j8
```

The output binary is `dist/M5CardputerZero-IR-Remote`.

## Usage

Run the SDL build:

```bash
./dist/M5CardputerZero-IR-Remote
```

Use a custom recordings directory:

```bash
IR_REMOTE_RECORDINGS_DIR=./ir-recordings ./dist/M5CardputerZero-IR-Remote
```

Packaged CardputerZero launches use `$HOME/.config/IR-Remote/signals` by default.

Useful environment variables:

- `IR_REMOTE_RECORDINGS_DIR`: exact directory for saved `.ir` files
- `IR_REMOTE_CONFIG_DIR`: app config root; recordings default to `$IR_REMOTE_CONFIG_DIR/signals`
- `IR_REMOTE_LIRC_RX_RC`: restrict receiver discovery to one rc sysfs path
- `IR_REMOTE_LIRC_TX_RC`: restrict transmitter discovery to one rc sysfs path
- `IR_REMOTE_LIRC_RX_DEVICE`: require one explicit receiver device path
- `IR_REMOTE_LIRC_TX_DEVICE`: require one explicit transmitter device path
- `IR_REMOTE_KEYBOARD_DEVICE`: Linux input device for hardware key events
- `IR_REMOTE_KEYBOARD_GRAB`: set to `1` to grab the keyboard input device
- `IR_REMOTE_SDL_ZOOM`: SDL window scale for desktop testing

By default, the device build scans all rc-core/LIRC nodes and selects them by
capability rather than node number:

```text
receiver     LIRC_CAN_REC_MODE2
transmitter  LIRC_CAN_SEND_PULSE
```

Explicit device and rc overrides are strict: discovery fails when the selected
node does not advertise the required capability.

Key controls:

- List page: `4` record, `5`/`6` or Up/Down select, `7` or Enter detail, `8` delete, `Esc` exit
- Detail page: `7` or Enter replay, `4` or Esc back

## Package

Build the cp0/CardputerZero Debian package:

```bash
./packaging/deb/package_deb.sh
```

The package script is device-targeted only. It always configures the
framebuffer/LIRC build and produces an `arm64` APPLaunch package.

The generated package is written to `dist/`:

```text
dist/m5cardputerzero-ir-remote_0.1.1_m5stack1_arm64.deb
```
