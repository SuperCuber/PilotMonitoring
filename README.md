# Pilot Monitoring

Windows X-Plane plugin scaffold linked with the X-Plane SDK and whisper.cpp.

## Development commands

The `libs/` directory contains local-only dependencies and is intentionally
ignored by Git. Set them up once from the project root with PowerShell:

```powershell
New-Item -ItemType Directory -Force libs | Out-Null

# Lua 5.4.7, used by the embedded command engine.
$luaArchive = Join-Path $env:TEMP 'lua-5.4.7.tar.gz'
Invoke-WebRequest https://www.lua.org/ftp/lua-5.4.7.tar.gz -OutFile $luaArchive
tar -xzf $luaArchive -C libs

# whisper.cpp, required by the voice service. The project currently builds
# against commit f133970bbb8c034ad9055a70afb97d61c24038f9.
$whisperCommit = 'f133970bbb8c034ad9055a70afb97d61c24038f9'
$whisperArchive = Join-Path $env:TEMP 'whisper.cpp.zip'
Invoke-WebRequest "https://github.com/ggml-org/whisper.cpp/archive/$whisperCommit.zip" -OutFile $whisperArchive
Expand-Archive $whisperArchive -DestinationPath libs
Move-Item "libs/whisper.cpp-$whisperCommit" libs/whisper.cpp
```

Download the X-Plane SDK 4.30 from the [official X-Plane developer site](https://developer.x-plane.com/sdk/),
then extract its `SDK` directory to `libs/XPSDK430/SDK`. The expected files
include `libs/XPSDK430/SDK/CHeaders/XPLM/XPLMPlugin.h` and
`libs/XPSDK430/SDK/Libraries/Win/XPLM_64.lib`.

The Whisper model is also local-only. Download `ggml-tiny.en.bin` from the
whisper.cpp model releases and place it at
`libs/whisper.cpp/models/ggml-tiny.en.bin` before building the plugin.

Run these from the project root in PowerShell:

```powershell
# Configure the Visual Studio build files.
cmake -B build

# Build the Release plugin package.
cmake --build build --config Release --target PilotMonitoring

# Build all test binaries and run all tests.
cmake --build build --config Release --target lua_engine_tests c172_tests
ctest --test-dir build -C Release --output-on-failure

# Run only one aircraft handler test.
ctest --test-dir build -C Release -R "^aircraft\.C172\.tune_radio$" --output-on-failure

# Once: link the build package into X-Plane (replace the X-Plane path first).
New-Item -ItemType SymbolicLink `
    -Path 'C:\X-Plane 12\Resources\plugins\PilotMonitoring' `
    -Target (Resolve-Path build\package\PilotMonitoring)
```

The symlink is a one-time setup. Afterward, run only the build command; the
X-Plane plugin folder always points at the latest package.

The finished plugin is at:

```text
build\package\PilotMonitoring\win_x64\PilotMonitoring.xpl
```

The package includes one Lua command file per supported aircraft, named after
its ICAO code (for example, `resources/C172.lua`). At plugin enable time,
Pilot Monitoring reads `sim/aircraft/view/acf_ICAO` and loads the matching Lua
file to register handlers, generate the Whisper grammar, and run the stateful
command engine at runtime.

If CMake/MSBuild cannot write to `C:\Temp`, set a project-local temporary
directory before configuring or building:

```powershell
New-Item -ItemType Directory -Force build\tmp | Out-Null
$env:TEMP = (Resolve-Path build\tmp)
$env:TMP = $env:TEMP
cmake -B build
cmake --build build --config Release --target PilotMonitoring
```
