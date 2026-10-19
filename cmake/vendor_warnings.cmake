function(panda_silence_target_warnings target_name)
    if (NOT TARGET ${target_name})
        return()
    endif ()

    get_target_property(target_type ${target_name} TYPE)
    if (target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif ()

    if (MSVC)
        target_compile_options(${target_name} PRIVATE /W0)
    else ()
        target_compile_options(${target_name} PRIVATE -w)
    endif ()
endfunction()

function(panda_silence_ios_precision_warnings target_name)
    if (NOT PLATFORM_IOS)
        return()
    endif ()

    if (NOT TARGET ${target_name})
        return()
    endif ()

    get_target_property(target_type ${target_name} TYPE)
    if (target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif ()

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE -Wno-shorten-64-to-32)
    endif ()

    if (CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(${target_name} PROPERTIES
            XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO
        )
    endif ()
endfunction()
