#pragma once

#include "PandaEditorKit/EditorExtensions.hpp"

// Entry point of the editor part of a module (context/plugins.md, stage 2): contributions to the
// editor registries (EditorExtensions). Symmetric to PANDA_PACKAGE_ENGINE_ENTRY:
//
//   PANDA_PACKAGE_EDITOR_ENTRY(MyModuleEditor::registerExtensions)
//
// The part is linked into the editor statically; the build defines
// PANDA_PACKAGE_EDITOR_STATIC_ID=pandaPackageEditor_<id as a C identifier> (panda_package()),
// the editor calls the entry point through the generated table of built-in modules when a project
// is opened.
#define PANDA_PACKAGE_EDITOR_ENTRY(registerFn)                                                                         \
    void PANDA_PACKAGE_EDITOR_STATIC_ID(Editor::EditorExtensions &extensions) {                                         \
        registerFn(extensions);                                                                                        \
    }
