# PilotMonitoring

Windows/X-Plane plugin built with CMake and vcpkg.

## Configure, build, and test

The production preset bootstraps the pinned vcpkg registry and installs Lua,
whisper.cpp, and the X-Plane SDK:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

The default build creates the complete package at:

```text
build\windows-release\package\PilotMonitoring
```

The plugin is located at:

```text
build\windows-release\package\PilotMonitoring\win_x64\PilotMonitoring.xpl
```

The package includes the Whisper model at
`resources/models/ggml-base.en.bin`, the aircraft command files, and the
positive and negative audio cues.

To run a single registered test:

```powershell
ctest --preset windows-release -R "^aircraft\.C172\.tune_radio$" --output-on-failure
```

Tests can also be run directly from the generated Release directory:

```powershell
.\build\windows-release\Release\c172_tests.exe tune_radio lineup_checklist
```

The C172 and B738 Lua files are loaded by the tests from the source
`resources/` directory. The installed plugin loads the matching file from its
packaged `resources/` directory based on the aircraft ICAO dataref.

## Installing into X-Plane

Install or link the staged `package\PilotMonitoring` directory under X-Plane's
`Resources\plugins` directory. For example, once the destination is adjusted:

```powershell
New-Item -ItemType SymbolicLink `
  -Path 'C:\X-Plane 12\Resources\plugins\PilotMonitoring' `
  -Target (Resolve-Path build\windows-release\package\PilotMonitoring)
```

## Sandbox limitation

CMake configuration does not work reliably inside the agent sandbox. The
configure step bootstraps vcpkg, downloads or extracts tools and dependencies,
and launches child processes that the sandbox may block. Configuration must be
run by the user in a normal PowerShell session outside the sandbox:

```powershell
cmake --preset windows-release
```

This also applies when `cmake --build --preset windows-release` detects a stale
build tree and automatically tries to regenerate it. Typical failure messages
include:

- `generate.stamp is out-of-date` followed by an automatic CMake rerun.
- `error: calling CreateFileW stdin failed with 5 (Access is denied.)`.
- `vcpkg install failed`.
- `No CMAKE_CXX_COMPILER could be found` during the failed regeneration.
- `CMake Configure step failed. Build files cannot be regenerated correctly.`

### Required behavior for agents

Agents must not attempt to work around a sandboxed configure failure. Do not
edit generated Visual Studio/CMake files, inject compiler flags into generated
projects, invoke MSBuild directly to bypass regeneration, copy dependencies
from another checkout, disable the sandbox, or try alternate configure
commands. Stop and ask the user to run `cmake --preset windows-release` outside
the sandbox. After the user confirms that configuration completed, agents may
run the normal build and test preset commands inside the sandbox.

Agents should make the same request whenever a build-system input such as
`CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, or a file under `cmake/`
changes and the existing build tree therefore requires regeneration.
