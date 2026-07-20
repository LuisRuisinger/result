# ================================================================================================
# Layout tests
# ================================================================================================

add_executable(result_layout_tests
        layout_tests.cpp
)

target_link_libraries(result_layout_tests
        PRIVATE
        lsr::result
)

# ================================================================================================
# clang-tidy
# ================================================================================================

if (RESULT_ENABLE_CLANG_TIDY)
    set_target_properties(result_layout_tests
            PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
    )
endif ()

# ================================================================================================
# Sanitizers
# ================================================================================================

if (RESULT_ENABLE_SANITIZERS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(result_layout_tests
                PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )

        target_link_options(result_layout_tests
                PRIVATE
                -fsanitize=address,undefined
        )
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