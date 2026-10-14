# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# test/standalone/materializer_dispatch_invalid.cmake
# ────────────────────────────────────────────────────────
execute_process(COMMAND "${TEST_EXECUTABLE}" --assertions
        RESULT_VARIABLE result OUTPUT_VARIABLE assertions ERROR_VARIABLE error)
if (NOT result STREQUAL "0" OR NOT assertions MATCHES "^(enabled|disabled)$")
    message(FATAL_ERROR "Cannot determine assertion mode: ${result}\n${assertions}\n${error}")
endif ()
foreach (kind signed-constant monostate-operator unrelated signed-unffor-width
        monostate-column string-column typed-string nested-list nested-struct)
    set(expected_operator "dec_constant_opr")
    set(expected_column "TypedCol")
    if (kind STREQUAL "monostate-operator")
        set(expected_operator "monostate")
    elseif (kind STREQUAL "unrelated")
        set(expected_operator "enc_fsst_opr")
    elseif (kind STREQUAL "signed-unffor-width")
        set(expected_operator "dec_unffor_opr")
    elseif (kind STREQUAL "monostate-column")
        set(expected_column "monostate")
    elseif (kind STREQUAL "string-column")
        set(expected_column "FLSStrColumn")
    elseif (kind STREQUAL "nested-list")
        set(expected_operator "PhysicalExpr")
        set(expected_column "List")
    elseif (kind STREQUAL "nested-struct")
        set(expected_operator "PhysicalExpr")
        set(expected_column "Struct")
    endif ()
    execute_process(COMMAND "${TEST_EXECUTABLE}" --invalid "${kind}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if (assertions STREQUAL "enabled")
        if (NOT result STREQUAL "Subprocess aborted" OR NOT error MATCHES "ABORTED" OR
                NOT error MATCHES "UNREACHABLE with unexpected types" OR
                NOT error MATCHES "materializer.cpp" OR NOT error MATCHES "${expected_operator}" OR
                NOT error MATCHES "${expected_column}")
            message(FATAL_ERROR "Expected assertion for ${kind}: ${result}\n${output}\n${error}")
        endif ()
    elseif (NOT result STREQUAL "0")
        message(FATAL_ERROR "Invalid materializer check failed for ${kind}: ${result}\n${output}\n${error}")
    endif ()
endforeach ()
