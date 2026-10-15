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

Run as `<resource name>\<Command>` (see `list`) or `ResourceEditorCommand -Asset <guid> -Cmd "<command>"`. Paths, values and node paths are text, in quotes.

| Command | |
|---|---|
| `ListProperties [-Filter text]` | every descriptor property with its value: the paths `SetProperty` takes |
| `SetProperty -Path -Value [-Before]` | one property (undoable). A list is resized with a path ending in `[]` |
| `ListOp -Path -Op Insert\|Delete\|Move -Index n [-ToIndex n]` | inserts, deletes or moves an element in the middle of a 1D array property (undoable); ordinal keys only |
| `ListNodes` | the scene nodes with their merge group and deleted state (after the first compile) |
| `AddNodeToNewGroup -Node`, `AddNodeToGroup -Node -Group <index>`, `RemoveNodeFromGroup -Node` | merge groups (undoable) |
| `DeleteNode -Node`, `UndeleteNode -Node` | leave a node and its children out of the compiled geometry (undoable) |
| `Save`, `Compile` | save the descriptor; validate, save and queue the compile |
| `Undo`, `Redo` | |
| `CompileStatus [-Lines n]` | how the last compile went: state, unsaved changes, validation errors, the end of the log |
| `SetCamera [-Yaw -Pitch -Distance -Target x,y,z]`, `GetCamera`, `FrameSubject` | the preview camera (degrees), read back, or refitted to the subject (view state, not undoable) |
| `ListPreview [-Filter]`, `SetPreview -Path -Value` | the preview settings (the ones on the Rendering Options panel), by the paths `ListPreview` prints; an enum takes its item name |
| `SetImportMaterials -On true\|false` | imports the materials and textures of the model or stops (undoable): the slots nobody assigned take the imported instances, or give them back |
| `DuplicateImportedMaterial -Slot i -Instance hex` | copies a slot's imported Material Instance into a resource of the project (next to the geometry) and makes it the slot's override (undoable) |
| `RemoveUnusedImportedResources` | removes the imported resources this one no longer makes (a material left the model, an import is off): Material Instances, Textures, Skeleton, Animations, with the edits they hold (undoable: the files and the library entries come back). The Description panel shows the button (with a confirmation) while there are some; the asset browser draws them dimmed. |
| `ApplyImportCompressions` | sets the compression of the generated textures to the `ImportMaterials` compressions (a color map, a color map that cuts out, a normal map, the ORME), overwriting what was chosen for them; textures of a constant stay as they are (undoable). The Description panel has the button, with a confirmation. |
| `Statistics` | meshes, vertices, submeshes and so on of the compiled geometry |

Each node command snapshots the descriptor first, so undo restores everything it touched (groups, ungrouped meshes, the deleted list and the
material reference counts). A node is named by its path from the scene root, e.g. `RootNode/Armature/Body`.

## The preview

Right drag turns the camera, middle drag pans, the wheel zooms, Space lets the light follow the camera. The left panel holds the view
settings (wire frame, tangents, binormals, normals, grid) and the statistics of the compiled geometry. Under it the Scene Hierarchy panel
shows the nodes of the model (right click a node: merge groups, delete); the Description panel on the right holds the descriptor. Every panel
docks anywhere.

## Imported materials

`ImportMaterials/bImport` on: every compile makes one virtual Material Instance per material of the model, from the PBR material of
`ImportMaterials/Material` (`ImportMaterials/AlphaTest` when the material is cut out by an opacity map or by the alpha of its color map), with
virtual Textures in its slots: the color map (or the diffuse color), the normal map (or a flat one) and the ORME packed from the PBR maps of
the model (or constants: roughness from the shininess, emission from the emissive color). They show as children of the geometry in the
resource browser and are ordinary resources: edit them in their own editors. A compile makes the ones that do not exist and updates only what the
geometry owns (of an instance its base material and the slots that still hold a texture the geometry made; of a texture nothing, the same texture
has the same input), the rest stays yours. The compression of the textures made from now on is in `ImportMaterials`: `AlbedoCompression` (BC1),
`AlbedoAlphaCompression` (BC7, color maps that cut out), `NormalCompression` (BC5), `ORMECompression` (BC7); a texture that exists is not changed by
them, and textures of a constant are never compressed. A slot can also take an instance of your own, and is then an
override. An override has the arrow beside it (it gives the slot back the imported instance); the row's context menu has
"Duplicate as override", which starts your own instance from the imported one. The code is in `xmaterial_instance.plugin`
(`source/xmaterial_instance_import.h`, `source/Compiler/xmaterial_instance_import_compiler.h`, `source/Editor/xmaterial_instance_import_editor.h`).
