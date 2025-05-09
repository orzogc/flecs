function(target_default_compile_warnings_c THIS)

    if (FLECS_STRICT)

        if (EMSCRIPTEN)
            # Less strict warnings for emscripten builds
            target_compile_options(${THIS} PRIVATE
                -Wall -Wextra
                -Wno-shadow
                -Wno-unused-parameter
                -Wno-missing-field-initializers
                -Wno-strict-prototypes)

        elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang"
                OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")

            target_compile_options(${THIS} PRIVATE
                    $<$<CONFIG:Debug>:-Wshadow>
                    $<$<CONFIG:Debug>:-Wunused>
                    -Wall -Wextra -Werror
                    -Wcast-align
                    -Wpedantic
                    -Wconversion
                    -Wsign-conversion
                    -Wdouble-promotion
                    -Wno-missing-prototypes
                    -Wno-missing-variable-declarations)

        elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")

            target_compile_options(${THIS} PRIVATE
                    $<$<CONFIG:Debug>:-Wshadow>
                    $<$<CONFIG:Debug>:-Wunused>
                    -Wall -Wextra -Werror
                    -Wcast-align
                    -Wpedantic
                    -Wconversion
                    -Wsign-conversion
                    -Wdouble-promotion
                    -Wno-missing-prototypes
                    -Wno-missing-variable-declarations)

        elseif (CMAKE_C_COMPILER_ID STREQUAL "MSVC")

            target_compile_options(${THIS} PRIVATE
                    /W3 /WX
                    /w14242 /w14254 /w14263
                    /w14265 /w14287 /we4289
                    /w14296 /w14311 /w14545
                    /w14546 /w14547 /w14549
                    /w14555 /w14619 /w14640
                    /w14826 /w14905 /w14906
                    /w14928)

        else ()

            message(WARNING
                    "No warning settings available for ${CMAKE_C_COMPILER_ID}. ")

        endif ()

    endif ()

endfunction()

function(target_default_compile_warnings_cxx THIS)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
            OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")

        target_compile_options(${THIS} PRIVATE
                #$<$<CONFIG:RELEASE>:-Werror>
                $<$<CONFIG:Debug>:-Wshadow>
                $<$<CONFIG:Debug>:-Wunused>
                -Wall -Wextra
                -Wnon-virtual-dtor
                -Wold-style-cast
                -Wcast-align
                -Woverloaded-virtual
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wdouble-promotion)

    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

        target_compile_options(${THIS} PRIVATE
                #$<$<CONFIG:RELEASE>:-Werror>
                $<$<CONFIG:Debug>:-Wshadow>
                $<$<CONFIG:Debug>:-Wunused>
                -Wall -Wextra
                -Wnon-virtual-dtor
                -Wold-style-cast
                -Wcast-align
                -Woverloaded-virtual
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wdouble-promotion)

    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")

        target_compile_options(${THIS} PRIVATE
                #$<$<CONFIG:RELEASE>:/WX>
                /W3
                /w14242 /w14254 /w14263
                /w14265 /w14287 /we4289
                /w14296 /w14311 /w14545
                /w14546 /w14547 /w14549
                /w14555 /w14619 /w14640
                /w14826 /w14905 /w14906
                /w14928)

    else ()

        message(WARNING
                "No Warnings specified for ${CMAKE_CXX_COMPILER_ID}. "
                "Consider using one of the following compilers: Clang, GNU, MSVC, AppleClang.")

    endif ()

endfunction()
