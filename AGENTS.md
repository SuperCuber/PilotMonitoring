# Build-system instructions for agents

- Never run a CMake configure or generate step inside the sandbox.
- An ordinary preset build may be run only against an already configured,
  up-to-date build tree.
- If a build reports that `generate.stamp` is out of date, starts an automatic
  CMake rerun, reports `CreateFileW stdin failed with 5`, fails during vcpkg
  installation, cannot find `CMAKE_CXX_COMPILER`, or says that build files
  cannot be regenerated, stop immediately.
- Do not bypass the failure by editing generated files, injecting compiler
  flags into generated projects, invoking MSBuild directly, copying artifacts
  or dependencies from another checkout, disabling sandbox protections, or
  trying alternate configure commands.
- Ask the user to run `cmake --preset windows-release` in a normal PowerShell
  session outside the sandbox. Resume normal preset builds and tests only after
  the user confirms that configuration succeeded.
- After changing `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, or files
  under `cmake/`, make the same request instead of attempting regeneration.
