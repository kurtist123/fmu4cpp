
function(make_test fmi_version modelIdentifier sources)
    set(target_name "${modelIdentifier}_${fmi_version}")
    add_executable(${target_name} ${sources} "$<TARGET_OBJECTS:fmu4cpp_base>" "$<TARGET_OBJECTS:fmu4cpp_${fmi_version}>")
    add_test(NAME ${target_name} COMMAND ${target_name})
    target_link_libraries(${target_name} PUBLIC Catch2::Catch2WithMain)
    target_compile_definitions(${target_name}
            PRIVATE
            FMU4CPP_MODEL_IDENTIFIER="${modelIdentifier}"
            TEST_CASE_RESOURCE_LOCATION="${PROJECT_SOURCE_DIR}/export/tests/resources"
    )
    target_include_directories(${target_name}
            PRIVATE
            "${PROJECT_SOURCE_DIR}/export/tests/models"
            "${PROJECT_SOURCE_DIR}/export/tests/external"
            "${PROJECT_SOURCE_DIR}/export/include"
            "${PROJECT_SOURCE_DIR}/export/src"
    )

endfunction()

function(make_generic_test modelIdentifier sources)
    set(target_name "${modelIdentifier}")
    add_executable(${target_name} ${sources} "$<TARGET_OBJECTS:fmu4cpp_base>")
    add_test(NAME ${target_name} COMMAND ${target_name})
    target_link_libraries(${target_name} PUBLIC Catch2::Catch2WithMain)
    target_compile_definitions(${target_name}
            PRIVATE
            FMU4CPP_MODEL_IDENTIFIER="${modelIdentifier}"
            TEST_CASE_RESOURCE_LOCATION="${PROJECT_SOURCE_DIR}/export/tests/resources"
    )
    target_include_directories(${target_name}
            PRIVATE
            "${PROJECT_SOURCE_DIR}/export/include"
            "${PROJECT_SOURCE_DIR}/export/src"
    )

endfunction()

make_generic_test(basic_test basic_test.cpp)
make_generic_test(test_resource test_resource.cpp)

add_subdirectory(fmi2)
add_subdirectory(fmi3)

# Build real FMU packages for test models
generateFMU(Identity
        FMI_VERSIONS fmi2 fmi3
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/models/identity.cpp"
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/models"
        DESTINATION "${CMAKE_BINARY_DIR}/models"
)

generateFMU(BouncingBall
        FMI_VERSIONS fmi2 fmi3
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/models/bouncing_ball.cpp"
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/models"
        DESTINATION "${CMAKE_BINARY_DIR}/models"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import lxml"
    RESULT_VARIABLE _lxml_check
    OUTPUT_QUIET
    ERROR_QUIET
)
if (NOT _lxml_check EQUAL 0)
    find_program(_SYSTEM_PYTHON3 NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH)
    if (_SYSTEM_PYTHON3)
        execute_process(
            COMMAND ${_SYSTEM_PYTHON3} -c "import lxml"
            RESULT_VARIABLE _sys_lxml_check
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if (_sys_lxml_check EQUAL 0)
            set(Python3_EXECUTABLE "${_SYSTEM_PYTHON3}" CACHE FILEPATH "Path to Python3 executable" FORCE)
            set(_lxml_check 0)
        endif()
    endif()
endif()

add_test(NAME validate_fmus
        COMMAND ${Python3_EXECUTABLE} "${_fmu4cpp_root}/scripts/validate_fmu.py"
        --dir "${CMAKE_BINARY_DIR}/models"
        --require-fmus
        --strict
        --check-symbols
        --expect-models Identity BouncingBall
        --expect-generations fmi2 fmi3
        -v
)

if (_lxml_check EQUAL 0)
    add_test(NAME validate_fmus_lxml
            COMMAND ${Python3_EXECUTABLE} "${_fmu4cpp_root}/scripts/validate_fmu.py"
            --dir "${CMAKE_BINARY_DIR}/models"
            --require-fmus
            --backend lxml
            --strict
            --check-symbols
            --expect-models Identity BouncingBall
            --expect-generations fmi2 fmi3
            -v
    )
endif()

add_test(NAME test_validator
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_validate_fmu.py"
        --fmu4cpp-root "${_fmu4cpp_root}"
        --build-dir "${CMAKE_BINARY_DIR}"
)



