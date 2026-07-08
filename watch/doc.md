# Quality Tools

Use these tools before committing firmware changes. This repo is C/C++ firmware for Arduino/ESP32 with an LVGL simulator build, so the checks focus on static analysis, simulator warnings, and complexity.

## Install System Tools

Arch/pacman:

```bash
sudo pacman -Syu cppcheck clang bear uv
```

`lizard` is managed locally by `uv` in this repository:

```bash
uv sync
```

## 1. Cppcheck

Run static analysis that works well with embedded C/C++ and non-standard include setups:

```bash
make -C sim cppcheck
```

The configured target scans editable firmware/simulator `.cpp` sources and skips generated LVGL image/font assets under `src/`. Headers are still analyzed through the `.cpp` files that include them, instead of being parsed as standalone C files.

It also suppresses `ctuOneDefinitionRuleViolation` because the current codebase has repeated local typedef names across independent UI translation units, which is noisy for this project.

## 2. Clang-Tidy

Generate `compile_commands.json` from the simulator build first:

```bash
make -C sim clang-tidy-db
```

Run `clang-tidy` on touched files or focused areas:

```bash
clang-tidy ui_main.cpp ui_weather.cpp -p .
```

## 3. Simulator Build Warnings

Always make sure the simulator still builds:

```bash
make -C sim
```

Run the stricter simulator build:

```bash
make -C sim quality-build
```

For stricter quality work, add or use a dedicated simulator target with warning flags such as:

```bash
-Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Wformat=2 -Wundef
```

Do not enable `-Werror` at first; the existing firmware code may have legacy warnings that should be cleaned incrementally.

## 4. Lizard

Run local complexity analysis through `uv`:

```bash
make -C sim lizard
```

Use this to find large or complex functions, especially in `ui_*.cpp`, parser code, and callback-heavy LVGL screens. The default target reports the current legacy baseline without failing the whole quality run.

To fail on complexity warnings, use:

```bash
make -C sim lizard-strict
```

## Recommended Local Check Set

```bash
make -C sim
make -C sim cppcheck
make -C sim lizard
SDL_VIDEODRIVER=dummy timeout 10 ./sim/sim
```

The `make -C sim smoke` target treats timeout exit code `124` as success because the simulator is expected to keep running until killed by `timeout`.

The same local set is available as:

```bash
make -C sim quality
```
