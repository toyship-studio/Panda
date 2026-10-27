project(PandaSmoke)

set(CMAKE_CXX_STANDARD 20)

# Engine smoke test (World → offscreen render → pixel checks): a bundle with the engine
# resources, as in PandaEditor — StaticResources load the builtin shaders/textures from the bundle.
file(GLOB RESOURCE_FOLDERS
    ${PANDA_DIRECTORY}/resources/*
)
list(APPEND RESOURCE_FOLDERS ${PANDA_ENGINE_GENERATED_RESOURCE_FOLDERS})
# The resources of the engine parts of the built-in modules (the ones switched on): data folders
# and baked shader folders, each as Resources/<folder>.
get_property(PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS)
get_property(PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS)
list(APPEND RESOURCE_FOLDERS ${PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS} ${PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS})
list(FILTER RESOURCE_FOLDERS EXCLUDE REGEX "\\.DS_Store")

add_app(
    PandaSmoke
    "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp"
    ""
    "${RESOURCE_FOLDERS}"
    RESOURCE_TARGET_DEPENDS
    ${PANDA_SHADER_BAKE_TARGET}
)

foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set_target_properties(PandaSmoke
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIRECTORY}
    )
endforeach()

set_target_properties(PandaSmoke
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BIN_DIRECTORY}
    VS_DEBUGGER_WORKING_DIRECTORY ${BIN_DIRECTORY}
)

target_link_libraries(PandaSmoke Panda)
# The modules the checks use; the build without modules skips what needs them. The terrain
# registers after the physics (its dependency).
if (TARGET PandaPhysics)
    target_link_libraries(PandaSmoke PandaPhysics)
    target_compile_definitions(PandaSmoke PRIVATE PANDA_SMOKE_HAS_PHYSICS=1)
endif ()
if (TARGET PandaTerrain)
    target_link_libraries(PandaSmoke PandaTerrain)
    target_compile_definitions(PandaSmoke PRIVATE PANDA_SMOKE_HAS_TERRAIN=1)
endif ()
