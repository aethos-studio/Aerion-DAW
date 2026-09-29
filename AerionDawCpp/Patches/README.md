# Patches to third-party code

`CMake/ApplyPatches.cmake` applies these at configure time, to whichever
source tree FetchContent uses (including a local
`FETCHCONTENT_SOURCE_DIR_TRACKTION_ENGINE` checkout). A patch already in the
tree is skipped; one that no longer applies stops the configure.

Keep patches small and generic. Put Aerion's own logic in Aerion's code
behind the hook a patch adds.

## tracktion/

| Patch | Why |
|---|---|
| `0001-external-plugin-process-hook.patch` | Adds `EngineBehaviour::processExternalPluginBlock`, which `ExternalPlugin` calls instead of calling the hosted plugin's `processBlock` directly. Aerion overrides it to catch plugin crashes (`Source/PluginFaultGuard.h`). |

To change a patch: edit the files in the engine tree, then regenerate it from
the engine checkout with `git diff -- modules > <patch>` (LF line endings).
When updating Tracktion Engine, check that each patch still applies.
