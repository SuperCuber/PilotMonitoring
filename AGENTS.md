# Build-system instructions for agents

- Never run a CMake configure or generate step inside the sandbox.
- Run the ordinary preset build without first checking whether the build tree
  is configured or up to date. Do not proactively ask the user to configure.
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
- Do not inspect timestamps or otherwise preflight whether changes to
  `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, or files under `cmake/`
  require regeneration. Run the requested build and follow the preceding
  failure guidance only if the build actually encounters the condition.
- The package target copies `PilotMonitoring.xpl` last. If that final copy fails
  because X-Plane is running, all other package files were already refreshed.
  This is a valid development condition. Notify the user that X-Plane is
  running and the packaged `.xpl` was not updated. Do not attempt to unlock,
  overwrite, rename, or delete the loaded plugin; the user can close X-Plane
  and rebuild when they want to update it.
