# Pilot Monitoring

Windows X-Plane plugin scaffold linked with the X-Plane SDK and whisper.cpp.

## Development commands

Run these from the project root in PowerShell:

```powershell
# Configure the Visual Studio build files.
cmake -B build

# Build the Release plugin package.
cmake --build build --config Release --target PilotMonitoring

# Build and run all tests.
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

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

The package also includes `resources/commands.json`, which is loaded at plugin
startup to generate the Whisper grammar and the command matcher used at
runtime.

If CMake/MSBuild cannot write to `C:\Temp`, use a project-local temporary
directory for that command:

```powershell
New-Item -ItemType Directory -Force build\tmp | Out-Null
$env:TEMP = (Resolve-Path build\tmp)
$env:TMP = $env:TEMP
cmake -B build
```
