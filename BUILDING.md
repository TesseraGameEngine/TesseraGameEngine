# Building Tessera Game Engine & Standalone Exporter

This guide details how to build the **Tessera 3D Game Engine**, the **Engine Unit Test Suite** (88 offline tests), and create the **Linux Standalone Export Templates** on both Linux and Windows.

---

## 1. Building on Linux (Ubuntu, Debian, Fedora, Arch Linux, SteamOS)

Tessera compiles natively on 64-bit Linux distributions with **C++20** support (GCC 11+ or Clang 14+), **Vulkan 1.2+**, and **X11 / Wayland**.

### 1.1 Install System Prerequisites

#### On Ubuntu / Debian Derivatives:
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    libtool-bin \
    libvulkan1 \
    libvulkan-dev \
    vulkan-tools \
    mesa-vulkan-drivers \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev
```

#### On Fedora / RHEL Derivatives:
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install -y \
    cmake \
    git \
    vulkan-loader-devel \
    vulkan-tools \
    mesa-vulkan-drivers \
    libX11-devel \
    libXrandr-devel \
    libXinerama-devel \
    libXcursor-devel \
    libXi-devel \
    autoconf \
    automake \
    libtool
```

#### On Arch Linux / SteamOS (Desktop Mode):
```bash
sudo pacman -S --needed \
    base-devel \
    cmake \
    git \
    vulkan-devel \
    vulkan-tools \
    vulkan-mesa-layers \
    libx11 \
    libxrandr \
    libxinerama \
    libxcursor \
    libxi
```

---

### 1.2 Install Third-Party Dependencies via vcpkg

If you do not already have Microsoft `vcpkg` installed on Linux:
```bash
cd ~
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
```

Install the required engine libraries:
```bash
~/vcpkg/vcpkg install \
    glfw3:x64-linux \
    glm:x64-linux \
    imgui[docking-experimental]:x64-linux \
    imguizmo:x64-linux \
    lua:x64-linux \
    joltphysics:x64-linux \
    unofficial-openfbx:x64-linux
```

> **Note on STB:** Header-only STB dependencies (`stb_image.h`, etc.) are bundled directly inside the repository under `thirdparty/stb/` and are automatically discovered by CMake.

---

### 1.3 Configure and Compile

Run CMake using an isolated build directory (e.g. `build-linux`):

```bash
cd /path/to/engine

# 1. Configure with the vcpkg toolchain
cmake -B build-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake

# 2. Build the full engine and editor
cmake --build build-linux --target Tessera -j$(nproc)

# 3. Build the unit test suite
cmake --build build-linux --target EngineTestSuite -j$(nproc)
```

> **CMake System Vulkan Detection:** The engine's `CMakeLists.txt` is pre-configured to prioritize the official system Vulkan loader (`/usr/lib/x86_64-linux-gnu/libvulkan.so`) rather than headless loaders, ensuring `VK_KHR_xcb_surface` and `VK_KHR_xlib_surface` windowing extensions are enabled.

---

### 1.4 Generating the Linux Standalone Export Template

Tessera uses Godot-style export templates. To generate and install the precompiled Linux runner template:

```bash
cmake --build build-linux --target linux_export_template
```

This creates [templates/linux/TesseraGameRunner.x86_64](file:///c:/Users/arron/Documents/git/engine/templates/linux/TesseraGameRunner.x86_64). When anyone exports a game via the editor, this template is packaged as `<GameTitle>.x86_64` for instant standalone execution on Linux and Steam Deck.

---

### 1.5 Running the Engine on Linux

#### Hardware Vulkan GPU (NVIDIA / AMD / Intel):
```bash
./build-linux/Tessera
```

Or run a scene directly in game mode:
```bash
./build-linux/Tessera --scene scenes/main.scene
```

#### Inside Virtual Machines (VMware, VirtualBox, Headless CI):
Virtual machine hypervisors typically provide OpenGL/Direct3D virtual drivers rather than hardware Vulkan drivers. Mesa provides **Lavapipe** (`lvp`), a high-performance CPU software rasterizer:
```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build-linux/Tessera --scene scenes/main.scene
```

#### Running the Test Suite:
```bash
./build-linux/EngineTestSuite
```
All **88 tests** will execute and report `88 PASSED, 0 FAILED`.

---

## 2. Building on Windows (10 / 11 64-bit)

### 2.1 Prerequisites
- **Visual Studio 2022 or newer** with the *Desktop development with C++* workload.
- **Vulkan SDK** (1.2 or newer) installed with `glslc` on your `PATH` or in `VULKAN_SDK`.
- All Windows thirdparty binaries and headers are bundled in `thirdparty/` out of the box.

### 2.2 Configure and Compile

```powershell
# 1. Configure CMake
cmake -B build -S .

# 2. Compile Engine in Release (or Debug)
cmake --build build --config Release --parallel

# 3. Run the Unit Test Suite
cmake --build build --config Release --target EngineTestSuite
.\build\Release\EngineTestSuite.exe

# 4. Launch the Editor
.\build\Release\Tessera.exe
```

---

## 3. How to Export a Standalone Linux Game

Tessera allows you to export standalone, turnkey Linux games directly from the editor running on **either Windows or Linux**.

### 3.1 Via the Editor UI
1. Launch Tessera and open your project or scene.
2. Press <kbd>Ctrl</kbd> + <kbd>B</kbd> (or go to **File → Export Standalone Game...**).
3. Set **Target Platform** to:
   ```
   Linux Standalone (x86_64 / SteamOS)
   ```
4. Enter your **Game Title** (e.g. `SpaceShooter`) and choose a **Destination Directory** (e.g. `exports/SpaceShooter_Linux`).
5. Choose your default display resolution or fullscreen toggle.
6. (Optional) Check **Include Dual Proton / Wine Compatibility Runner** if you want fallback Windows `.exe` binaries bundled for hybrid environments.
7. Click **Export Game**.

### 3.2 Structure of an Exported Linux Game

The exported package is completely turnkey and contains:

```text
exports/SpaceShooter_Linux/
├── SpaceShooter.x86_64        # Native 64-bit Linux ELF binary (from export template)
├── launch.sh                  # Turnkey POSIX launch script with auto-permissions
├── SpaceShooter.desktop       # FreeDesktop app & Steam shortcut
├── game.agconfig              # Game configuration manifest (resolution, main scene)
├── input_bindings.json        # Unified keyboard, mouse & gamepad bindings
├── README_LINUX.txt           # Player guide & Steam Deck addition instructions
├── shaders/                   # Precompiled SPIR-V shaders (.spv)
├── scenes/                    # Serialized .scene hierarchy files
├── assets/                    # 3D models, PBR textures, audio (.wav)
└── scripts/                   # Lua 5.4 gameplay scripts
```

### 3.3 Running on Linux / SteamOS

1. Open a terminal in the exported game directory:
   ```bash
   chmod +x launch.sh
   ./launch.sh
   ```
2. **On Steam Deck / Steam Client**:
   - Open Steam on desktop or Steam Deck.
   - Click **Games** → **Add a Non-Steam Game to My Library...**
   - Browse to `launch.sh` (or `SpaceShooter.x86_64`) and add it.
   - Launch directly with full Steam Deck controller and resolution support!

