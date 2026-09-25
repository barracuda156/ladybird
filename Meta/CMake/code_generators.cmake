#
# Functions for generating sources using host tools
#

function(embed_as_string name source_file output source_variable_name)
    cmake_parse_arguments(PARSE_ARGV 4 EMBED_STRING_VIEW "" "NAMESPACE" "")
    set(namespace_arg "")
    if (EMBED_STRING_VIEW_NAMESPACE)
        set(namespace_arg "-s ${EMBED_STRING_VIEW_NAMESPACE}")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${Python3_EXECUTABLE}" "${SerenityOS_SOURCE_DIR}/Meta/embed_as_string.py" "${source_file}" -o "${output}.tmp" -n "${source_variable_name}" ${namespace_arg}
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${output}.tmp" "${output}"
        COMMAND "${CMAKE_COMMAND}" -E remove "${output}.tmp"
        VERBATIM
        DEPENDS "${SerenityOS_SOURCE_DIR}/Meta/embed_as_string.py"
        MAIN_DEPENDENCY "${source_file}"
    )

    add_custom_target("generate_${name}" DEPENDS "${output}")
    add_dependencies(ladybird_codegen_accumulator "generate_${name}")
endfunction()

function(compile_ipc source output)
    if (NOT IS_ABSOLUTE ${source})
        set(source ${CMAKE_CURRENT_SOURCE_DIR}/${source})
    endif()
    add_custom_command(
        OUTPUT ${output}
        COMMAND $<TARGET_FILE:Lagom::IPCCompiler> ${source} -o ${output}.tmp
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${output}.tmp ${output}
        COMMAND "${CMAKE_COMMAND}" -E remove ${output}.tmp
        VERBATIM
        DEPENDS Lagom::IPCCompiler
        MAIN_DEPENDENCY ${source}
    )
    get_filename_component(output_name ${output} NAME)
    add_custom_target(generate_${output_name} DEPENDS ${output})
    add_dependencies(ladybird_codegen_accumulator generate_${output_name})

    # Install the endpoint where it sits relative to the Lagom build directory, which mirrors the
    # include roots (., Services, Libraries) that consumers of the installed package get.
    cmake_path(ABSOLUTE_PATH output BASE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} NORMALIZE OUTPUT_VARIABLE output_path)
    cmake_path(RELATIVE_PATH output_path BASE_DIRECTORY ${Lagom_BINARY_DIR} OUTPUT_VARIABLE output_relative_path)
    cmake_path(GET output_relative_path PARENT_PATH output_relative_dir)
    if (ENABLE_INSTALL_HEADERS)
        install(FILES ${output_path} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${output_relative_dir}" OPTIONAL)
    endif()
endfunction()
