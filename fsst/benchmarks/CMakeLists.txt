# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# fsst/benchmarks/CMakeLists.txt
# ────────────────────────────────────────────────────────
# Reuse the production object targets without pulling in all of FastLanes.
# The helper implementations also reference Buf and ExternalMemory (including
# their debug assertions). Keep these objects in an archive so unrelated
# common/I/O implementations are not linked in.
add_library(fsst_benchmark_support STATIC)
target_link_libraries(fsst_benchmark_support
        PRIVATE
        fls_fsst_prm
        fls_fsst12_prm
        fls_primitive_fsst
        fls_primitive_fsst12
        fls_lyt
        fls_io
        fls_common
        PUBLIC
        FastLanes::headers)
target_compile_definitions(fsst_benchmark_support PUBLIC FLS_STATIC)

add_executable(test_fsst test_fsst.cpp)
target_link_libraries(test_fsst PRIVATE gtest_main fsst_benchmark_support)
gtest_discover_tests(test_fsst)

add_executable(test_fls_fsst test_fls_fsst.cpp)
target_link_libraries(test_fls_fsst PRIVATE gtest_main fsst_benchmark_support)
gtest_discover_tests(test_fls_fsst)
