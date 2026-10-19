if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

set(_input_file "${INPUT_FILE}")

if(DEFINED ENV{EFFECTIVE_PLATFORM_NAME})
    string(REPLACE "\${EFFECTIVE_PLATFORM_NAME}" "$ENV{EFFECTIVE_PLATFORM_NAME}" _input_file "${_input_file}")
endif()

if(NOT EXISTS "${_input_file}")
    message(FATAL_ERROR "Input file does not exist: ${_input_file}")
endif()

get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_input_file}" "${OUTPUT_FILE}"
    RESULT_VARIABLE _copy_result
)

if(NOT _copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to copy ${_input_file} to ${OUTPUT_FILE}")
endif()
