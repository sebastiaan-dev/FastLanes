# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# test/standalone/variant_dispatch_invalid.cmake
# ────────────────────────────────────────────────────────
# Assertion-enabled builds abort on invalid alternatives; NDEBUG builds throw
# and the executable checks the exception and cursor state. Query the binary
# rather than inferring NDEBUG from a build configuration's name.
execute_process(COMMAND "${TEST_EXECUTABLE}" --assertions
        RESULT_VARIABLE result OUTPUT_VARIABLE assertions ERROR_VARIABLE error)
if (NOT result STREQUAL "0" OR NOT assertions MATCHES "^(enabled|disabled)$")
    message(FATAL_ERROR "Cannot determine assertion mode: ${result}\n${assertions}\n${error}")
endif ()
foreach (type i08_pt i16_pt i32_pt i64_pt u08_pt u16_pt u32_pt u64_pt)
    foreach (operation enc_rsum enc_transpose enc_slpatch dec_rsum nested_rsum dec_slpatch dec_transpose)
        if (operation MATCHES "slpatch")
            set(callsite "slpatch_operator.cpp")
        elseif (operation MATCHES "transpose")
            set(callsite "transpose_operator.cpp")
        else ()
            set(callsite "rsum_operator.cpp")
        endif ()
        foreach (alternative monostate unrelated mismatch)
            if (alternative STREQUAL "monostate")
                set(expected_type "monostate")
            elseif (alternative STREQUAL "unrelated")
                if (operation STREQUAL "enc_transpose")
                    set(expected_type "dec_fsst_opr")
                else ()
                    set(expected_type "enc_fsst_opr")
                endif ()
            elseif (operation STREQUAL "enc_rsum" OR operation STREQUAL "enc_transpose")
                set(expected_type "enc_scan_opr")
            elseif (operation STREQUAL "enc_slpatch")
                set(expected_type "enc_analyze_opr")
            elseif (operation STREQUAL "dec_slpatch")
                set(expected_type "dec_unffor_opr")
            elseif (operation STREQUAL "dec_transpose")
                set(expected_type "dec_rsum_opr")
            else ()
                set(expected_type "dec_slpatch_opr")
            endif ()
            execute_process(
                    COMMAND "${TEST_EXECUTABLE}" "${operation}" "${alternative}" "${type}"
                    RESULT_VARIABLE result
                    OUTPUT_VARIABLE output
                    ERROR_VARIABLE error)
            if (assertions STREQUAL "enabled")
                if (NOT result STREQUAL "Subprocess aborted" OR NOT error MATCHES "ABORTED" OR
                        NOT error MATCHES "UNREACHABLE" OR NOT error MATCHES "${callsite}" OR
                        NOT error MATCHES "${expected_type}")
                    message(FATAL_ERROR "Expected assertion for ${type}/${operation}/${alternative}: ${result}\n${output}\n${error}")
                endif ()
            elseif (NOT result STREQUAL "0")
                message(FATAL_ERROR "Invalid-alternative check failed for ${type}/${operation}/${alternative}: ${result}\n${output}\n${error}")
            endif ()
        endforeach ()
    endforeach ()
endforeach ()
