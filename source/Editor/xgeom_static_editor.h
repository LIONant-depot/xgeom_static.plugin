#ifndef XGEOM_STATIC_EDITOR_H
#define XGEOM_STATIC_EDITOR_H
#pragma once

// The Static Geom editor: opens a static geometry resource from the asset browser in its own window. The descriptor is edited in an
// inspector and through the node commands (merge groups, deleted nodes), all undoable; the compiled geometry is previewed in 3D next
// to its statistics. Hosts include this header and open editors through xeditor::open_resource_editors.
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_InspectorPickers.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_descriptor.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor_preview.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_Resources.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_rsc_loader.cpp"      // the resource loader: compiled once, in the host's translation unit

#include <charconv>
#include <functional>

namespace xgeom_static_editor
{
    //--------------------------------------------------------------------------------------------
    // Node commands. A node is named by its path from the root of the imported scene ("Root/Body/Wheel", base64 on the command line).
    // Each one snapshots the descriptor first, so undo puts back everything it touched (groups, ungrouped meshes, deleted list, material counts).
    //--------------------------------------------------------------------------------------------
    struct node_cmd : xundo::command_base
    {
        using apply_fn = std::string(*)(xgeom_static::descriptor&, const xgeom_static::details&, const std::string& Path, int Group);

        xeditor::descriptor_document&   m_Doc;
        xgeom_static::details&          m_Details;
        const char*                     m_pHelp;
        apply_fn                        m_Apply;
        bool                            m_bGroupArg;

        node_cmd(xundo::system& System, xeditor::descriptor_document& Doc, xgeom_static::details& Details, const char* pName, const char* pHelp, apply_fn Apply, bool bGroupArg) noexcept
            : command_base(System, pName, nullptr), m_Doc(Doc), m_Details(Details), m_pHelp(pHelp), m_Apply(Apply), m_bGroupArg(bGroupArg) { RegisterArguments(); }

        const char* getCommandHelp() const noexcept override { return m_pHelp; }
        void RegisterArguments() noexcept override
        {
            m_hNode = m_Parser.addOption("Node", "Node path from the scene root, base64", true, 1);
            if (m_bGroupArg) m_hGroup = m_Parser.addOption("Group", "Index of the merge group", true, 1);
        }

        std::string Redo() noexcept override
        {
            std::string Node, Group;
            if (!xeditor::cmd_util::GetArg(m_Parser, m_hNode, Node) || (m_bGroupArg && !xeditor::cmd_util::GetArg(m_Parser, m_hGroup, Group)))
                return std::format("{}: bad arguments", m_pCommandName);
            if (!m_Doc.m_pDescriptor) return std::format("{}: nothing loaded", m_pCommandName);

            int iGroup = 0;
            if (m_bGroupArg) std::from_chars(Group.data(), Group.data() + Group.size(), iGroup);

            const auto Path = xeditor::Base64Decode(Node);
            if (!m_Details.findNode(xgeom_static::SplitNodePath(Path)).first) return std::format("{}: no node '{}' (compile the geometry first)", m_pCommandName, Path);

            auto Err = m_Apply(static_cast<xgeom_static::descriptor&>(*m_Doc.m_pDescriptor), m_Details, Path, iGroup);
            if (!Err.empty()) return std::format("{}: {}", m_pCommandName, Err);
            m_Doc.m_bDirty = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override { xeditor::WriteString(File, m_Doc.Snapshot()); }
        void Undo(xundo::undo_file& File) noexcept override               { m_Doc.Restore(xeditor::ReadString(File)); }

        xcmdline::parser::handle m_hNode, m_hGroup;
    };

    inline std::string AddToNewGroup(xgeom_static::descriptor& D, const xgeom_static::details&, const std::string& Path, int)
    {
        for (int i = 0; i < 100; ++i)
        {
            auto Name = std::format("Group #{}", i);
            if (std::ranges::any_of(D.m_MergeGroupList, [&](auto& G) { return G.m_Name == Name; })) continue;
            auto& Group = D.m_MergeGroupList.emplace_back();
            Group.m_Name = std::move(Name);
            D.AddNodeInGroupList(Group, Path);
            return {};
        }
        return "too many merge groups";
    }

    inline std::string AddToGroup(xgeom_static::descriptor& D, const xgeom_static::details&, const std::string& Path, int Group)
    {
        if (Group < 0 || Group >= static_cast<int>(D.m_MergeGroupList.size())) return std::format("no merge group {}", Group);
        D.AddNodeInGroupList(D.m_MergeGroupList[Group], Path);
        return {};
    }

    inline std::string RemoveFromGroup(xgeom_static::descriptor& D, const xgeom_static::details& Details, const std::string& Path, int)
    {
        auto Pair = D.findMergeGroupFromNode(Path);
        if (!Pair.first) return "the node is not in a merge group";
        D.RemoveNodeFromGroup(*Pair.first, Pair.second, Details);
        return {};
    }

    inline std::string DeleteNode(xgeom_static::descriptor& D, const xgeom_static::details& Details, const std::string& Path, int)
    {
        if (D.isNodeInDeleteList(Path)) return "the node is already deleted";
        D.AddNodeInDeleteList(Path, Details);
        return {};
    }

    inline std::string UndeleteNode(xgeom_static::descriptor& D, const xgeom_static::details& Details, const std::string& Path, int)
    {
        if (!std::ranges::any_of(D.m_DeleteEntryList, [&](auto& E) { return E.m_MeshName.empty() && E.m_NodePath == Path; })) return "the node itself is not deleted";
        D.RemoveNodeFromDeleteList(Path, Details);
        return {};
    }

    // The nodes with the state that decides what the compiler does with them, one per line: path, group, deleted.
    struct list_nodes_cmd : xundo::query_command_base
    {
        xeditor::descriptor_document&   m_Doc;
        xgeom_static::details&          m_Details;
        list_nodes_cmd(xundo::system& System, xeditor::descriptor_document& Doc, xgeom_static::details& Details) noexcept : query_command_base(System, "ListNodes", nullptr), m_Doc(Doc), m_Details(Details) {}
        const char* getCommandHelp() const noexcept override { return "Lists the scene nodes of the compiled geometry with their merge group and deleted state. Usage: ListNodes"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            if (!m_Doc.m_pDescriptor) return "ListNodes: nothing loaded";
            if (m_Details.m_RootNode.m_Name.empty()) return "ListNodes: no node information yet (compile the geometry first)";
            auto& D = static_cast<xgeom_static::descriptor&>(*m_Doc.m_pDescriptor);

            std::string Out;
            std::function<void(const xgeom_static::details::node&, const std::string&)> Walk = [&](const xgeom_static::details::node& Node, const std::string& Path)
            {
                Out += Path;
                if (auto* pGroup = D.findMergeGroupFromNode(Path).first) Out += std::format("  [group {}]", pGroup->m_Name);
                if (D.isNodeInDeleteList(Path)) Out += "  [deleted]";
                for (auto i : Node.m_MeshList) Out += std::format("  mesh:{}", m_Details.m_MeshList[i].m_Name);
                Out += '\n';
                for (auto& Child : Node.m_Children) Walk(Child, Path + "/" + Child.m_Name);
            };
            Walk(m_Details.m_RootNode, m_Details.m_RootNode.m_Name);
            return Out;
        }
    };

    //--------------------------------------------------------------------------------------------
    // The editor
    //--------------------------------------------------------------------------------------------
    struct session : xeditor::descriptor_editor
    {
        xgeom_static::details               m_Details;                  // the scene nodes, from the compiler's log (empty until the first compile)
        node_cmd                            m_AddToNewGroup, m_AddToGroup, m_RemoveFromGroup, m_DeleteNode, m_UndeleteNode;
        list_nodes_cmd                      m_ListNodes;

        preview::runtime                    m_Preview;
        static_geom_inspector               m_Info;
        xeditor::inspector_panel            m_SettingsInspector{ "Rendering Settings" };
        xrsc::geom_static                   m_GeomRef;
        bool                                m_bHasGeom = false;         // the compiled geometry is loaded and drawable

        session(xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("StaticGeom", Guid, LibraryGuid, pDevice)
            , m_AddToNewGroup   (m_Undo, m_Document, m_Details, "AddNodeToNewGroup",  "Puts a node in a new merge group (undoable). Usage: AddNodeToNewGroup -Node base64",            &AddToNewGroup,   false)
            , m_AddToGroup      (m_Undo, m_Document, m_Details, "AddNodeToGroup",     "Puts a node in a merge group (undoable). Usage: AddNodeToGroup -Node base64 -Group index",      &AddToGroup,      true)
            , m_RemoveFromGroup (m_Undo, m_Document, m_Details, "RemoveNodeFromGroup","Takes a node out of its merge group (undoable). Usage: RemoveNodeFromGroup -Node base64",         &RemoveFromGroup, false)
            , m_DeleteNode      (m_Undo, m_Document, m_Details, "DeleteNode",         "Leaves a node (and its children) out of the compiled geometry (undoable). Usage: DeleteNode -Node base64", &DeleteNode, false)
            , m_UndeleteNode    (m_Undo, m_Document, m_Details, "UndeleteNode",       "Puts a deleted node back (undoable). Usage: UndeleteNode -Node base64",                         &UndeleteNode,    false)
            , m_ListNodes(m_Undo, m_Document, m_Details)
        {
            for (auto* pInspector : { &m_DescriptorInspector.m_Inspector, &m_SettingsInspector.m_Inspector })
                e10::WireResourcePickerCallbacks(*pInspector);
            RegisterMaterialRowLabels(m_DescriptorInspector.m_Inspector);

            m_Preview.m_Settings.clear();
            m_SettingsInspector.BindObject(*xproperty::getObject(m_Preview.m_Settings), &m_Preview.m_Settings);
            m_SettingsInspector.AppendComponent(*xproperty::getObject(m_Info), &m_Info);

            AddPanel("Rendering Settings", dock::left,   [this] { m_SettingsInspector.Show(); });
            AddPanel("Preview",            dock::center, [this] { RenderPreview(); });
            AddPanel("Description",        dock::right,  [this] { RenderHierarchy(); m_DescriptorInspector.Show(); });

            if (pDevice && m_Preview.Init(*pDevice)) ReloadResource();
        }

        ~session() noexcept override { xresource::g_Mgr.ReleaseRef(m_GeomRef); }

        void OnCompiled() noexcept override { ReloadResource(); }

        // Each material slot of the descriptor is labelled with the material's name, and dimmed when the current nodes no longer use it.
        static void RegisterMaterialRowLabels(xproperty::inspector& Inspector) noexcept
        {
            Inspector.m_OnResourceLeftSize.m_Delegates.clear();
            Inspector.m_OnResourceLeftSize.Register<[](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any&, ImGuiTreeNodeFlags Flags, const char* pName, bool& Open)
            {
                std::string NewName;
                bool bDisable = false;

                // Only the material slots are relabelled: the delegate is called for every resource row of the inspector. (Matched by
                // path: the property table object differs between translation units, so its address says nothing.)
                if (Path.find("/MaterialInstance[") != std::string_view::npos)
                {
                    const auto Bracket = Path.rfind('[');
                    const auto Colon   = Bracket == std::string_view::npos ? std::string_view::npos : Path.find(':', Bracket);
                    const auto Close   = Bracket == std::string_view::npos ? std::string_view::npos : Path.find(']', Bracket);
                    auto pDesc = static_cast<xgeom_static::descriptor*>(pInstance);
                    if (Colon != std::string_view::npos && Close != std::string_view::npos && Colon < Close && !pDesc->m_MaterialDetailsList.empty())
                    {
                        const auto Text = Path.substr(Colon + 1, Close - Colon - 1);
                        int Index = 0;
                        if (std::from_chars(Text.data(), Text.data() + Text.size(), Index).ec == std::errc() && Index >= 0 && Index < static_cast<int>(pDesc->m_MaterialDetailsList.size()))
                        {
                            NewName  = std::format("{} {}", pName, pDesc->m_MaterialDetailsList[Index].m_Name);
                            pName    = NewName.c_str();
                            bDisable = pDesc->m_MaterialDetailsList[Index].m_RefCount <= 0;
                        }
                    }
                }

                if (bDisable) ImGui::BeginDisabled(true);
                if (!Path.empty()) Open = ImGui::TreeNodeEx(reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path)), ImGuiTreeNodeFlags_Framed | Flags, "  %s", pName);
                else               Open = ImGui::TreeNodeEx(pName, Flags);
                if (bDisable) ImGui::EndDisabled();
            }>();
        }

        // Loads the compiled geometry (after a compile, or when the editor opens on an already compiled one): the statistics, the
        // preview's materials and the scene nodes the compiler left in its log.
        void ReloadResource() noexcept
        {
            if (!m_Preview.m_bReady || !std::filesystem::exists(m_Document.m_ResourcePath)) return;

            xresource::g_Mgr.ReleaseRef(m_GeomRef);
            m_GeomRef.m_Instance = m_Document.m_Guid.m_Instance;
            auto* pGeom = xresource::g_Mgr.getResource(m_GeomRef);
            m_bHasGeom = pGeom != nullptr;
            if (!pGeom) return;

            m_Info.Update(m_GeomRef, m_Document.m_ResourcePath);
            m_Preview.RebuildMaterials(*pGeom);
            LoadDetails();
        }

        // The compiler's list of nodes and meshes. The descriptor is brought in line with it: new meshes appear, vanished ones are dropped.
        void LoadDetails() noexcept
        {
            xtextfile::stream File;
            if (auto Err = File.Open(true, m_Document.m_LogPath + L"\\Details.txt", {}); Err) return;

            m_Details = {};
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(File, m_Details, Context); Err) { m_Details = {}; return; }

            const auto Before = m_Document.Snapshot();
            for (auto& Message : static_cast<xgeom_static::descriptor&>(*m_Document.m_pDescriptor).MergeWithDetails(m_Details))
                std::printf("%s\n", Message.c_str());
            if (m_Document.Snapshot() != Before) m_Document.m_bDirty = true;
        }

        void RenderPreview() noexcept
        {
            const ImVec2 Avail = ImGui::GetContentRegionAvail();
            ImGui::InvisibleButton("##GeomPreviewCanvas", Avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            m_Preview.HandleInput();

            auto* pHost   = xeditor::host::current();
            auto* pWindow = pHost ? pHost->find<xgpu::window>() : nullptr;
            auto* pGeom   = m_bHasGeom ? xresource::g_Mgr.getResource(m_GeomRef) : nullptr;

            if (!m_Preview.m_bReady || !pWindow) { ImGui::TextDisabled("Preview needs a GPU device (open from E29)."); return; }
            if (!pGeom)                          { ImGui::TextUnformatted("No compiled resource yet (compile the geometry)."); return; }

            m_Preview.RenderShadow(*pWindow, *pGeom, Avail.x, Avail.y);
            xgpu::tools::imgui::AddCustomRenderCallback([this, Avail](xgpu::cmd_buffer& CmdBuffer, const ImVec2&, const ImVec2&)
            {
                if (!m_bOpen || !m_bHasGeom) return;
                if (auto* p = xresource::g_Mgr.getResource(m_GeomRef)) m_Preview.Draw(CmdBuffer, *p, Avail.x, Avail.y);
            });
        }

        // The scene nodes. Right click: merge groups and deleting. Every action is a command, run once the tree is drawn.
        void RenderHierarchy() noexcept
        {
            auto& Root = m_Details.m_RootNode;
            if (!m_Document.m_pDescriptor || (Root.m_Children.empty() && Root.m_MeshList.empty())) return;
            xeditor::PushLevelEditorInspectorHeaderColors();
            const bool bShow = ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen);
            xeditor::PopLevelEditorInspectorHeaderColors();
            if (!bShow) return;

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 7));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

            constexpr ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
            auto&  Desc         = static_cast<xgeom_static::descriptor&>(*m_Document.m_pDescriptor);
            const auto TextColor    = ImGui::GetStyle().Colors[ImGuiCol_Text];
            const auto GroupColor   = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            const auto DeletedColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f);
            std::string Pending;                                    // the command a menu item asked for

            // A node with nothing to show below it is not drawn
            std::function<bool(const xgeom_static::details::node&)> WorthRendering = [&](const xgeom_static::details::node& N)
            {
                if (!N.m_MeshList.empty()) return true;
                return std::ranges::any_of(N.m_Children, [&](auto& C) { return WorthRendering(C); });
            };

            auto Command = [&](const char* pName, const std::string& Path) { return std::format("{} -Node {}", pName, xeditor::Base64Encode(Path)); };

            std::string Path = Root.m_Name;
            std::function<void(const xgeom_static::details::node&, bool, bool)> DisplayNode = [&](const xgeom_static::details::node& N, bool bIncluded, bool bDeletedParent)
            {
                if (!WorthRendering(N)) return;

                const size_t PrevLength = Path.size();
                Path += "/" + N.m_Name;

                const bool  bInDeleteList = Desc.isNodeInDeleteList(Path);
                const bool  bDeleted      = bDeletedParent || bInDeleteList;
                const auto  Pair          = Desc.findMergeGroupFromNode(Path);
                const char* pGroupName    = Pair.first ? Pair.first->m_Name.c_str() : "";

                if (bDeleted) ImGui::PushStyleColor(ImGuiCol_Text, DeletedColor);
                const bool bOpen = bInDeleteList ? ImGui::TreeNodeEx(&N, Flags, "\xEE\x9D\x8D (%s) %s", pGroupName, N.m_Name.c_str())
                                 : Pair.first    ? ImGui::TreeNodeEx(&N, Flags, "\xEE\xAF\x92 (%s) %s", pGroupName, N.m_Name.c_str())
                                                 : ImGui::TreeNodeEx(&N, Flags, "%s", N.m_Name.c_str());
                if (bDeleted) ImGui::PopStyleColor();

                ImGui::PushID(&N);
                if (ImGui::BeginPopupContextItem("NodeContextMenu"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, TextColor);
                    if (!bIncluded)
                    {
                        if (Pair.first)
                        {
                            if (ImGui::MenuItem("Remove from Group")) Pending = Command("RemoveNodeFromGroup", Path);
                        }
                        else
                        {
                            if (ImGui::MenuItem("Add to New Group")) Pending = Command("AddNodeToNewGroup", Path);
                            if (!Desc.m_MergeGroupList.empty() && ImGui::BeginMenu("Add to Merge Group"))
                            {
                                for (int i = 0; i < static_cast<int>(Desc.m_MergeGroupList.size()); ++i)
                                    if (ImGui::MenuItem(Desc.m_MergeGroupList[i].m_Name.c_str())) Pending = std::format("AddNodeToGroup -Node {} -Group {}", xeditor::Base64Encode(Path), i);
                                ImGui::EndMenu();
                            }
                        }
                    }
                    if (!bDeleted)          { if (ImGui::MenuItem("\xEE\x9D\x8D Delete Node"))   Pending = Command("DeleteNode", Path); }
                    else if (bInDeleteList) { if (ImGui::MenuItem("\xEE\x9D\x8D UnDelete Node")) Pending = Command("UndeleteNode", Path); }
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                if (bOpen)
                {
                    if (bDeleted) ImGui::PushStyleColor(ImGuiCol_Text, DeletedColor);
                    else if (Pair.first) ImGui::PushStyleColor(ImGuiCol_Text, GroupColor);

                    for (int Index : N.m_MeshList)
                    {
                        const auto& Mesh = m_Details.m_MeshList[Index];
                        ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(Index)), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, "\xEE\xAF\x92 %s", Mesh.m_Name.c_str());
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::PushStyleColor(ImGuiCol_Text, TextColor);
                            ImGui::Text("nFaces    : %d\nnUVs      : %d\nnColors   : %d\nnMaterials: %d\n", Mesh.m_NumFaces, Mesh.m_NumUVs, Mesh.m_NumColors, static_cast<int>(Mesh.m_MaterialList.size()));
                            for (auto& Mat : Mesh.m_MaterialList)
                                ImGui::Text("%2d.%s\n", 1 + static_cast<int>(&Mat - Mesh.m_MaterialList.data()), m_Details.m_MaterialList[Mat].c_str());
                            ImGui::PopStyleColor();
                            ImGui::EndTooltip();
                        }
                    }

                    for (auto& Child : N.m_Children) DisplayNode(Child, Pair.first || bIncluded, bDeleted);
                    ImGui::TreePop();

                    if (bDeleted || Pair.first) ImGui::PopStyleColor();
                }

                Path.resize(PrevLength);
            };

            const bool bRootOpen = ImGui::TreeNodeEx(&Root, Flags, Desc.m_bMergeAllMeshes ? "\xEE\xAF\x92 Root" : "Root");
            if (bRootOpen)
            {
                if (Desc.m_bMergeAllMeshes) ImGui::PushStyleColor(ImGuiCol_Text, GroupColor);
                for (auto& Child : Root.m_Children) DisplayNode(Child, Desc.m_bMergeAllMeshes, false);
                ImGui::TreePop();
                if (Desc.m_bMergeAllMeshes) ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar(4);

            if (!Pending.empty()) xeditor::Run(m_Undo, Pending);
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { xrsc::geom_static_type_guid_v
    , [](xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };
}

#endif // XGEOM_STATIC_EDITOR_H
