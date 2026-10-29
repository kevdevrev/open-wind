find_package(Catch2 3 CONFIG REQUIRED)

add_executable(openwind_tests
    test_main.cpp
    test_windfield.cpp
    "${PROJECT_SOURCE_DIR}/src/Runtime/WindField.cpp"
    "${PROJECT_SOURCE_DIR}/src/Runtime/TransientWindImpulse.cpp"
)
target_compile_features(openwind_tests PRIVATE cxx_std_23)
target_compile_definitions(openwind_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(openwind_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(openwind_tests PRIVATE Catch2::Catch2 Microsoft::DirectXTK)
add_test(NAME openwind_tests COMMAND openwind_tests)
