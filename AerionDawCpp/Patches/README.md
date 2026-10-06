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
| `0002-external-plugin-call-hook.patch` | Adds `EngineBehaviour::callExternalPlugin`, through which `ExternalPlugin` makes its other calls into the hosted plugin: creating the instance (synchronous path), `getStateInformation` when flushing its state and `setStateInformation` when restoring it. When the call reports failure, the plugin's saved state is left as it was. Aerion overrides it to catch crashes there too. |

Patches apply in file-name order, each on top of the ones before it. Keep their
hunks at least four lines apart: the configure step tests each patch on its own
to see whether it is already in the tree, which fails when a later patch has
changed the context lines of an earlier one. To change
one, edit the files in the engine tree, then regenerate that patch as the
difference from the tree with only the earlier patches applied (LF line
endings, paths relative to the engine checkout, `a/` and `b/` prefixes). With a
single patch, `git diff -- modules > <patch>` from the engine checkout does it.
When updating Tracktion Engine, check that each patch still applies.
