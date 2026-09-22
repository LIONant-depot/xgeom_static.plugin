# The Static Geom editor

Double-click a Static Geom resource in the asset browser (or `OpenResourceEditor -Asset <guid>`) and it opens in its own window: a 3D
preview of the compiled geometry, its statistics, and the descriptor with the scene nodes the compiler found. Everything the window
changes goes through commands on the editor's own undo system, so an AI or a script does the same edits from the command console.

```
source/Editor/xgeom_static_editor.h                the editor (session), the node commands, its registration
source/Editor/xgeom_static_editor_preview.h        the 3D preview: shadow pass, lit geometry, wire frame, debug lines, grid
source/Editor/xgeom_static_editor_inspectors.h     view settings (camera, light, debug, grid) and the read-only statistics
source/Editor/shaders/                             the wire frame, normal-lines and grid shaders
```

A host includes `xgeom_static_editor.h`: that registers the editor for the `GeomStatic` type (`xeditor::open_resource_editors` opens it) and
compiles the resource loader (`xgeom_static_xgpu_rsc_loader.cpp`) into the host. The host provides an `xgpu::device` and the main
`xgpu::window` as `xeditor::host` services: the preview draws the light's view into a shadow map with a render pass on the window before the
frame's UI is rendered, then draws the lit scene from the preview panel's render callback.

The generic half (document, `SetProperty`, `Save`, `Compile`, `Undo`, the window and its dock space) is `xeditor::descriptor_editor`
(`source/Tools/Editor/xeditor_descriptor_editor.h` in the xGPU tree); this depot adds what is specific to static geometry.

## Commands

Run as `<resource name>\<Command>` (see `list`) or `ResourceEditorCommand -Asset <guid> -Cmd <base64>`. Paths, values and node paths are base64.

| Command | |
|---|---|
| `ListProperties [-Filter text]` | every descriptor property with its value: the paths `SetProperty` takes |
| `SetProperty -Path -Value [-Before]` | one property (undoable). A list is resized with a path ending in `[]` |
| `ListNodes` | the scene nodes with their merge group and deleted state (after the first compile) |
| `AddNodeToNewGroup -Node`, `AddNodeToGroup -Node -Group <index>`, `RemoveNodeFromGroup -Node` | merge groups (undoable) |
| `DeleteNode -Node`, `UndeleteNode -Node` | leave a node and its children out of the compiled geometry (undoable) |
| `Save`, `Compile` | save the descriptor; validate, save and queue the compile |
| `Undo`, `Redo` | |
| `CompileStatus [-Lines n]` | how the last compile went: state, unsaved changes, validation errors, the end of the log |
| `SetCamera [-Yaw -Pitch -Distance -Target x,y,z]`, `GetCamera`, `FrameSubject` | the preview camera (degrees), read back, or refitted to the subject (view state, not undoable) |
| `ListPreview [-Filter]`, `SetPreview -Path -Value` | the preview settings (the ones on the Rendering Options panel), by the paths `ListPreview` prints; an enum takes its item name |
| `Statistics` | meshes, vertices, submeshes and so on of the compiled geometry |

Each node command snapshots the descriptor first, so undo restores everything it touched (groups, ungrouped meshes, the deleted list and the
material reference counts). A node is named by its path from the scene root, e.g. `RootNode/Armature/Body`.

## The preview

Right drag turns the camera, middle drag pans, the wheel zooms, Space lets the light follow the camera. The left panel holds the view
settings (wire frame, tangents, binormals, normals, grid) and the statistics of the compiled geometry.
