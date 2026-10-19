# panda_package([STATIC_TARGET <name>] [EDITOR_STATIC_TARGET <name>]) — one CMakeLists per module
# (context/plugins.md). Reads the module.json next to it (id, version, parts) and builds STATIC
# libraries — modules are linked into the host, there is no dynamic loading. A part is a folder
# named after it (engine/, editor/, sdk/) with the shape of a library inside: include/, src/,
# resources/:
#  * the engine part (engine/src/Panda/*.cpp, public engine/include/) with the entry point
#    pandaPackageEngine_<id as a C identifier> (Panda/Packages/PackageEntry.hpp); in the engine tree
#    it is linked with Panda (editor, smoke test), in the game export — with PandaSDK::Panda from the
#    sources that the SDK package carries in modules/<Folder>;
#  * the editor part (editor/src/Panda/*.cpp, editor/include; only in the engine tree, where
#    PandaEditorKit exists) with the entry point pandaPackageEditor_<id>
#    (PandaEditorKit/EditorPackageEntry.hpp) — linked into the editor; its editor/resources/*
#    folders get into the editor bundle;
#  * the SDK part (sdk/include/Bamboo — headers of the module's scripting API, sdk/src — their
#    implementation against the host table) — in the engine tree it is compiled into the PandaSDK
#    library, the headers ship in the SDK package as include/Bamboo/…;
#  * the resources of the engine part (engine/resources/<folder>, named after the module:
#    terrain-shaders) mirror Panda/resources: every folder goes into Resources/<folder> of the
#    hosts that link the module and into the SDK package, a folder of shaders (*.hlsl) is baked by
#    the common baker (cmake/shader_bake.cmake) and its generated folder takes its place.
# A module is not built on its own: it is part of the distribution (the tree) or of a game (the export).
# Hosts read the global properties: PANDA_STATIC_MODULE_{TARGETS,ENTRIES,IDS,VERSIONS,DEPENDENCIES} —
# the engine parts (the export generates pandaRegisterModules, the editor — the built-in table;
# DEPENDENCIES — the ids of the manifest's dependencies joined with commas, "none" without any),
# PANDA_BUILTIN_MODULE_{DIRS,EDITOR_TARGETS,EDITOR_ENTRIES,SDK_DIRS} and the resource folders
# PANDA_BUILTIN_MODULE_{RUNTIME,SHADER,EDITOR}_RESOURCE_FOLDERS (data folders, generated shader
# folders, editor folders) — the engine tree.
# The PANDA_PACKAGE_ID / PANDA_PACKAGE_VERSION defines — the module's passport from the manifest
# into the code.
function(panda_package)
    cmake_parse_arguments(PANDA_PACKAGE "" "STATIC_TARGET;EDITOR_STATIC_TARGET" "" ${ARGN})

    if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        message(FATAL_ERROR "panda_package(): modules build inside the engine tree or a game export, not standalone")
    endif ()
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/module.json)
        message(FATAL_ERROR "panda_package(): no module.json in ${CMAKE_CURRENT_SOURCE_DIR}")
    endif ()
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/module.json _PANDA_PACKAGE_MANIFEST)
    string(JSON _PANDA_PACKAGE_ID GET "${_PANDA_PACKAGE_MANIFEST}" id)
    string(JSON _PANDA_PACKAGE_VERSION GET "${_PANDA_PACKAGE_MANIFEST}" version)
    string(MAKE_C_IDENTIFIER "${_PANDA_PACKAGE_ID}" _PANDA_PACKAGE_C_ID)

    string(JSON _PANDA_PACKAGE_PARTS_COUNT LENGTH "${_PANDA_PACKAGE_MANIFEST}" parts)
    set(_PANDA_PACKAGE_HAS_ENGINE FALSE)
    set(_PANDA_PACKAGE_HAS_EDITOR FALSE)
    set(_PANDA_PACKAGE_HAS_SDK FALSE)
    if (_PANDA_PACKAGE_PARTS_COUNT GREATER 0)
        math(EXPR _PANDA_PACKAGE_PARTS_LAST "${_PANDA_PACKAGE_PARTS_COUNT} - 1")
        foreach (_PART_INDEX RANGE ${_PANDA_PACKAGE_PARTS_LAST})
            string(JSON _PART GET "${_PANDA_PACKAGE_MANIFEST}" parts ${_PART_INDEX})
            if (_PART STREQUAL engine)
                set(_PANDA_PACKAGE_HAS_ENGINE TRUE)
            elseif (_PART STREQUAL editor)
                set(_PANDA_PACKAGE_HAS_EDITOR TRUE)
            elseif (_PART STREQUAL sdk)
                set(_PANDA_PACKAGE_HAS_SDK TRUE)
            endif ()
        endforeach ()
    endif ()
    if (NOT _PANDA_PACKAGE_HAS_ENGINE)
        message(FATAL_ERROR "panda_package(): ${_PANDA_PACKAGE_ID} has no engine part in module.json parts")
    endif ()
    # The dependencies of the manifest ([{id, version}]): the hosts enable a module only with them.
    set(_PANDA_PACKAGE_DEPENDENCIES "")
    string(JSON _PANDA_PACKAGE_DEPENDENCY_COUNT ERROR_VARIABLE _PANDA_PACKAGE_JSON_ERROR LENGTH "${_PANDA_PACKAGE_MANIFEST}" dependencies)
    if (NOT _PANDA_PACKAGE_JSON_ERROR AND _PANDA_PACKAGE_DEPENDENCY_COUNT GREATER 0)
        math(EXPR _PANDA_PACKAGE_DEPENDENCY_LAST "${_PANDA_PACKAGE_DEPENDENCY_COUNT} - 1")
        foreach (_DEPENDENCY_INDEX RANGE ${_PANDA_PACKAGE_DEPENDENCY_LAST})
            string(JSON _DEPENDENCY_ID GET "${_PANDA_PACKAGE_MANIFEST}" dependencies ${_DEPENDENCY_INDEX} id)
            if (_PANDA_PACKAGE_DEPENDENCIES STREQUAL "")
                set(_PANDA_PACKAGE_DEPENDENCIES "${_DEPENDENCY_ID}")
            else ()
                set(_PANDA_PACKAGE_DEPENDENCIES "${_PANDA_PACKAGE_DEPENDENCIES},${_DEPENDENCY_ID}")
            endif ()
        endforeach ()
    endif ()
    if (_PANDA_PACKAGE_DEPENDENCIES STREQUAL "")
        set(_PANDA_PACKAGE_DEPENDENCIES none)
    endif ()

    # ---- Engine part ----
    if (NOT PANDA_PACKAGE_STATIC_TARGET)
        set(PANDA_PACKAGE_STATIC_TARGET ${_PANDA_PACKAGE_C_ID}_static)
    endif ()
    set(_PANDA_PACKAGE_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
    file(GLOB_RECURSE _PANDA_PACKAGE_SOURCES CONFIGURE_DEPENDS ${_PANDA_PACKAGE_ENGINE_DIR}/src/Panda/*.cpp)
    file(GLOB_RECURSE _PANDA_PACKAGE_HEADERS CONFIGURE_DEPENDS
        ${_PANDA_PACKAGE_ENGINE_DIR}/src/Panda/*.hpp
        ${_PANDA_PACKAGE_ENGINE_DIR}/include/Panda/*.hpp
    )
    add_library(${PANDA_PACKAGE_STATIC_TARGET} STATIC ${_PANDA_PACKAGE_SOURCES} ${_PANDA_PACKAGE_HEADERS})
    target_include_directories(${PANDA_PACKAGE_STATIC_TARGET} PRIVATE ${_PANDA_PACKAGE_ENGINE_DIR}/src)
    target_include_directories(${PANDA_PACKAGE_STATIC_TARGET} PUBLIC ${_PANDA_PACKAGE_ENGINE_DIR}/include)
    target_compile_definitions(${PANDA_PACKAGE_STATIC_TARGET} PRIVATE
        PANDA_PACKAGE_STATIC_ID=pandaPackageEngine_${_PANDA_PACKAGE_C_ID}
        PANDA_PACKAGE_ID="${_PANDA_PACKAGE_ID}"
        PANDA_PACKAGE_VERSION="${_PANDA_PACKAGE_VERSION}"
    )
    target_compile_features(${PANDA_PACKAGE_STATIC_TARGET} PRIVATE cxx_std_20)
    if (TARGET Panda)
        target_link_libraries(${PANDA_PACKAGE_STATIC_TARGET} PUBLIC Panda)
        set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}) # to the SDK package
        if (_PANDA_PACKAGE_HAS_SDK AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
            set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_SDK_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
        endif ()
    else ()
        target_link_libraries(${PANDA_PACKAGE_STATIC_TARGET} PUBLIC PandaSDK::Panda)
    endif ()
    set_property(GLOBAL APPEND PROPERTY PANDA_STATIC_MODULE_TARGETS ${PANDA_PACKAGE_STATIC_TARGET})
    set_property(GLOBAL APPEND PROPERTY PANDA_STATIC_MODULE_ENTRIES pandaPackageEngine_${_PANDA_PACKAGE_C_ID})
    set_property(GLOBAL APPEND PROPERTY PANDA_STATIC_MODULE_IDS ${_PANDA_PACKAGE_ID})
    set_property(GLOBAL APPEND PROPERTY PANDA_STATIC_MODULE_VERSIONS ${_PANDA_PACKAGE_VERSION})
    set_property(GLOBAL APPEND PROPERTY PANDA_STATIC_MODULE_DEPENDENCIES ${_PANDA_PACKAGE_DEPENDENCIES})

    # ---- Editor part: only where the editor exists (the engine tree) ----
    if (_PANDA_PACKAGE_HAS_EDITOR AND TARGET PandaEditorKit)
        if (NOT PANDA_PACKAGE_EDITOR_STATIC_TARGET)
            set(PANDA_PACKAGE_EDITOR_STATIC_TARGET ${_PANDA_PACKAGE_C_ID}_editor_static)
        endif ()
        set(_PANDA_PACKAGE_EDITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/editor)
        file(GLOB_RECURSE _PANDA_PACKAGE_EDITOR_SOURCES CONFIGURE_DEPENDS ${_PANDA_PACKAGE_EDITOR_DIR}/src/Panda/*.cpp)
        file(GLOB_RECURSE _PANDA_PACKAGE_EDITOR_HEADERS CONFIGURE_DEPENDS
            ${_PANDA_PACKAGE_EDITOR_DIR}/src/Panda/*.hpp
            ${_PANDA_PACKAGE_EDITOR_DIR}/include/Panda/*.hpp
        )
        add_library(${PANDA_PACKAGE_EDITOR_STATIC_TARGET} STATIC ${_PANDA_PACKAGE_EDITOR_SOURCES} ${_PANDA_PACKAGE_EDITOR_HEADERS})
        target_include_directories(${PANDA_PACKAGE_EDITOR_STATIC_TARGET} PRIVATE ${_PANDA_PACKAGE_EDITOR_DIR}/src)
        target_include_directories(${PANDA_PACKAGE_EDITOR_STATIC_TARGET} PUBLIC ${_PANDA_PACKAGE_EDITOR_DIR}/include)
        target_compile_definitions(${PANDA_PACKAGE_EDITOR_STATIC_TARGET} PRIVATE
            PANDA_PACKAGE_EDITOR_STATIC_ID=pandaPackageEditor_${_PANDA_PACKAGE_C_ID}
            PANDA_PACKAGE_ID="${_PANDA_PACKAGE_ID}"
            PANDA_PACKAGE_VERSION="${_PANDA_PACKAGE_VERSION}"
        )
        target_compile_features(${PANDA_PACKAGE_EDITOR_STATIC_TARGET} PRIVATE cxx_std_20)
        target_link_libraries(${PANDA_PACKAGE_EDITOR_STATIC_TARGET} PUBLIC PandaEditorKit ${PANDA_PACKAGE_STATIC_TARGET})
        set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_EDITOR_TARGETS ${PANDA_PACKAGE_EDITOR_STATIC_TARGET})
        set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_EDITOR_ENTRIES pandaPackageEditor_${_PANDA_PACKAGE_C_ID})
    elseif (TARGET PandaEditorKit)
        set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_EDITOR_ENTRIES none) # no editor part
    endif ()
    # ---- Resources (the engine tree) ----
    if (TARGET Panda)
        # The engine part: engine/resources/<folder> mirrors Panda/resources. A folder of shaders is
        # represented by its generated folder (the bake replaces the sources), any other folder
        # by itself; the hosts and the SDK package take both lists.
        file(GLOB _PANDA_PACKAGE_RESOURCE_FOLDERS LIST_DIRECTORIES true CONFIGURE_DEPENDS ${_PANDA_PACKAGE_ENGINE_DIR}/resources/*)
        foreach (_FOLDER ${_PANDA_PACKAGE_RESOURCE_FOLDERS})
            if (NOT IS_DIRECTORY ${_FOLDER})
                continue()
            endif ()
            file(GLOB_RECURSE _FOLDER_SHADERS CONFIGURE_DEPENDS ${_FOLDER}/*.hlsl)
            get_filename_component(_FOLDER_NAME ${_FOLDER} NAME)
            if (_FOLDER_SHADERS)
                set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS
                    ${PANDA_SHADER_GENERATED_ROOT}/${_FOLDER_NAME})
            else ()
                set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS ${_FOLDER})
            endif ()
        endforeach ()
    endif ()
    if (TARGET PandaEditorKit)
        # The editor part: editor/resources/<folder> (icons etc.) → Resources/<folder> of the
        # editor bundle only.
        file(GLOB _PANDA_PACKAGE_EDITOR_RESOURCE_FOLDERS LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/resources/*)
        foreach (_FOLDER ${_PANDA_PACKAGE_EDITOR_RESOURCE_FOLDERS})
            if (IS_DIRECTORY ${_FOLDER})
                set_property(GLOBAL APPEND PROPERTY PANDA_BUILTIN_MODULE_EDITOR_RESOURCE_FOLDERS ${_FOLDER})
            endif ()
        endforeach ()
    endif ()
endfunction()
