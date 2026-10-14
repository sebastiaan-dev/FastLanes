# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# test/standalone/CMakeLists.txt
# ────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
add_executable(fastlanes_integration_test integration_test.cpp)
target_link_libraries(fastlanes_integration_test PRIVATE FastLanes Threads::Threads)
add_test(NAME fastlanes_integration_test COMMAND fastlanes_integration_test)
set_tests_properties(fastlanes_integration_test PROPERTIES TIMEOUT 60)

add_executable(fastlanes_variant_dispatch_test variant_dispatch_test.cpp)
target_link_libraries(fastlanes_variant_dispatch_test PRIVATE FastLanes)
add_test(NAME fastlanes_variant_dispatch_test COMMAND fastlanes_variant_dispatch_test)
add_test(NAME fastlanes_variant_dispatch_invalid
        COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:fastlanes_variant_dispatch_test>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/variant_dispatch_invalid.cmake)
set_tests_properties(fastlanes_variant_dispatch_test fastlanes_variant_dispatch_invalid PROPERTIES TIMEOUT 120)

add_executable(fastlanes_materializer_dispatch_test materializer_dispatch_test.cpp)
target_link_libraries(fastlanes_materializer_dispatch_test PRIVATE FastLanes)
add_test(NAME fastlanes_materializer_dispatch_test COMMAND fastlanes_materializer_dispatch_test)
add_test(NAME fastlanes_materializer_dispatch_invalid
        COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:fastlanes_materializer_dispatch_test>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/materializer_dispatch_invalid.cmake)
set_tests_properties(fastlanes_materializer_dispatch_test fastlanes_materializer_dispatch_invalid PROPERTIES TIMEOUT 120)
