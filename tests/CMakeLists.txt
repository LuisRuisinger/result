# ================================================================================================
# Test executables
# ================================================================================================

set(RESULT_TEST_TARGETS
        result_layout_tests
        result_behavior_tests
)

add_executable(result_layout_tests
        layout_tests.cpp
)

add_executable(result_behavior_tests
        behavior_tests.cpp
)

foreach (target IN LISTS RESULT_TEST_TARGETS)
    target_link_libraries(${target}
            PRIVATE
            lsr::result
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target}
                PRIVATE
                -Wall
                -Wextra
                -Wconversion
        )
    endif ()
endforeach ()

# ================================================================================================
# Freestanding and no-exceptions builds
#
# Compiled, never linked. -ffreestanding sets __STDC_HOSTED__ to 0, which swaps the header over
# to its own memcpy / construct_at / index_sequence / string_view; -fno-exceptions removes
# unwrap_or_throw and the exception types. Both paths are large enough to rot silently, so they
# are built as part of `all`.
# ================================================================================================

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_library(result_freestanding_tests OBJECT
            freestanding_tests.cpp
    )

    target_link_libraries(result_freestanding_tests
            PRIVATE
            lsr::result
    )

    target_compile_options(result_freestanding_tests
            PRIVATE
            -ffreestanding
            -fno-exceptions
            -fno-rtti
            -Wall
            -Wextra
            -Wconversion
    )

    add_library(result_no_exceptions_tests OBJECT
            freestanding_tests.cpp
    )

    target_link_libraries(result_no_exceptions_tests
            PRIVATE
            lsr::result
    )

    target_compile_options(result_no_exceptions_tests
            PRIVATE
            -fno-exceptions
            -Wall
            -Wextra
            -Wconversion
    )
endif ()

# ================================================================================================
# clang-tidy
# ================================================================================================

if (RESULT_ENABLE_CLANG_TIDY)
    set_target_properties(${RESULT_TEST_TARGETS}
            PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
    )
endif ()

# ================================================================================================
# Sanitizers
# ================================================================================================

if (RESULT_ENABLE_SANITIZERS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        foreach (target IN LISTS RESULT_TEST_TARGETS)
            target_compile_options(${target}
                    PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
            )

            target_link_options(${target}
                    PRIVATE
                    -fsanitize=address,undefined
            )
        endforeach ()
    else ()
        message(WARNING
                "RESULT_ENABLE_SANITIZERS is currently supported only with GCC and Clang"
        )
    endif ()
endif ()

# ================================================================================================
# CTest registration
# ================================================================================================

add_test(
        NAME result.layout
        COMMAND result_layout_tests
)

add_test(
        NAME result.behavior
        COMMAND result_behavior_tests
)

# ================================================================================================
# Rejection tests
#
# Each case in reject_tests.cpp must fail to compile. The cases are built as targets excluded
# from `all`, and ctest drives the build of each one with WILL_FAIL set. A case that starts
# compiling is a silently reopened hole, which is the failure mode worth catching here.
# ================================================================================================

set(RESULT_REJECT_CASES 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)

foreach (case IN LISTS RESULT_REJECT_CASES)
    add_executable(result_reject_${case} EXCLUDE_FROM_ALL
            reject_tests.cpp
    )

    target_link_libraries(result_reject_${case}
            PRIVATE
            lsr::result
    )

    target_compile_definitions(result_reject_${case}
            PRIVATE
            REJECT_CASE=${case}
    )

    add_test(
            NAME result.reject.${case}
            COMMAND ${CMAKE_COMMAND}
            --build ${CMAKE_BINARY_DIR}
            --target result_reject_${case}
            --config $<CONFIG>
    )

    set_tests_properties(result.reject.${case}
            PROPERTIES
            WILL_FAIL TRUE
    )
endforeach ()
