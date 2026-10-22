include(cmake/platform.cmake)

if(PLATFORM_WIN32)

    function(add_target_platform_app TARGET_NAME SOURCE INCLUDE ASSETS)
        add_executable(${TARGET_NAME} ${SOURCE} ${INCLUDE})
    endfunction()

elseif(PLATFORM_ANDROID)

    function(add_target_platform_app TARGET_NAME SOURCE INCLUDE ASSETS)
        add_library(${TARGET_NAME} SHARED ${SOURCE} ${INCLUDE} ${ASSETS})
    endfunction()

elseif(PLATFORM_LINUX)

    function(add_target_platform_app TARGET_NAME SOURCE INCLUDE ASSETS)
        add_executable(${TARGET_NAME} ${SOURCE} ${INCLUDE})
    endfunction()

elseif(PLATFORM_MACOS)

    function(add_target_platform_app TARGET_NAME SOURCE INCLUDE ASSETS)

        # The asset catalog (AppIcon) is compiled only by the Xcode generator; a make build
        # would copy it into Resources as an empty folder — the icon is there anyway as a
        # separate .icns.
        if (CMAKE_GENERATOR STREQUAL Xcode)
            set(APPLE_RESOURCES
                    ${PROJECT_SOURCE_DIR}/native/OSX/AssetsOSX.xcassets
                    )
        else ()
            set(APPLE_RESOURCES "")
        endif ()

        set(ASSETS ${APPLE_RESOURCES})

        add_executable(${TARGET_NAME} MACOSX_BUNDLE ${SOURCE} ${INCLUDE} ${ASSETS})

        set_target_properties(${TARGET_NAME} PROPERTIES
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.md.${TARGET_NAME}"
                MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/native/OSX/Info.plist"
                # CFBundleShortVersionString and CFBundleVersion of the plist template (the root
                # CMakeLists reads the version, the release workflow passes the build number).
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PANDA_VERSION}"
                MACOSX_BUNDLE_BUNDLE_VERSION "${PANDA_BUILD_NUMBER}"
                # Signing is not part of the build: the release workflow signs the bundle with
                # native/OSX/PandaEditor.entitlements (scripts/release/sign_macos.sh).
                XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
                RESOURCE "${ASSETS}"
                )
        source_group("Resources" FILES "${ASSETS}")

        target_compile_definitions(${TARGET_NAME} PUBLIC GL_SILENCE_DEPRECATION)

    endfunction()

elseif(PLATFORM_IOS)

    function(add_target_platform_app TARGET_NAME SOURCE INCLUDE ASSETS)

        set(APPLE_RESOURCES
                ${PROJECT_SOURCE_DIR}/native/IOS/LaunchScreen.storyboard
                ${PROJECT_SOURCE_DIR}/native/IOS/Assets.xcassets
                )

        set(ASSETS ${APPLE_RESOURCES})

        add_executable(${TARGET_NAME} MACOSX_BUNDLE ${SOURCE} ${INCLUDE} ${ASSETS})

        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum iOS deployment version")

        set_target_properties(${TARGET_NAME} PROPERTIES
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.md.${TARGET_NAME}"
                MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/native/IOS/Info.plist"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PANDA_VERSION}"
                MACOSX_BUNDLE_BUNDLE_VERSION "${PANDA_BUILD_NUMBER}"
                XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
                RESOURCE "${ASSETS}"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
                BUILD_RPATH "@executable_path"
                # XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "Dev Team"
                )
        source_group("Resources" FILES "${ASSETS}")

        target_compile_definitions(${TARGET_NAME} PUBLIC GL_SILENCE_DEPRECATION)
        target_compile_definitions(${TARGET_NAME} PUBLIC GLES_SILENCE_DEPRECATION)

    endfunction()

else()
    message(FATAL_ERROR "Unknown platform")
endif()
