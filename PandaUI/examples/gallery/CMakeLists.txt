# The PandaUI gallery as a library (context/pandaui.md): every element of the library with a live
# example, one section per file in src/Sections. The editor shows it in a tab (Help → UI Gallery);
# the standalone example of this folder runs it on a backend of PandaUI/backends. It is pure
# PandaUI: textures, the display font, extra windows and the theme switch come from the
# application through PandaUI::Gallery::Host (include/PandaUIGallery/Gallery.hpp).

file(GLOB_RECURSE PANDAUI_GALLERY_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE PANDAUI_GALLERY_HEADERS CONFIGURE_DEPENDS src/*.hpp include/PandaUIGallery/*.hpp)

add_library(PandaUIGallery STATIC ${PANDAUI_GALLERY_SOURCES} ${PANDAUI_GALLERY_HEADERS})

target_include_directories(PandaUIGallery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(PandaUIGallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PandaUIGallery PUBLIC PandaUI)

# The images of the gallery (the logo of the Image section). A host copies the folder into its
# resources as it is and loads "panda-ui-gallery/<file>" through its own backend.
set_target_properties(PandaUIGallery PROPERTIES
    PANDAUI_GALLERY_RESOURCE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/resources/panda-ui-gallery"
)
