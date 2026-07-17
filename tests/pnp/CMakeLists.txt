# PNP fork: tests for the preset -> pnp config translator (F01) and its
# schema guard. The translator is pure (libslic3r + nlohmann only), so it
# compiles straight into the test binary without the GUI library.
add_executable(pnp_config_translator_tests
    test_pnp_config_translator.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/PnpConfigTranslator.cpp
)
target_include_directories(pnp_config_translator_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/slic3r/GUI)
target_link_libraries(pnp_config_translator_tests test_common libslic3r Catch2::Catch2WithMain)

orcaslicer_copy_test_dlls()
orcaslicer_discover_tests(pnp_config_translator_tests)
