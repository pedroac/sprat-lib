function(sprat_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE sprat::sprat)
    # Internal headers, for testing the tokenizer directly.
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${name} PRIVATE
        SPRAT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        SPRAT_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
        SPRAT_EXPECTED_VERSION="${SPRAT_LIB_VERSION_TAG}")
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # Generous for sanitizer builds; catches inputs that stop loading in linear time.
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# test_reader reaches into the library's internals, which a shared build hides.
if(NOT BUILD_SHARED_LIBS)
    sprat_add_test(test_reader)
endif()
sprat_add_test(test_sheet)
sprat_add_test(test_errors)
sprat_add_test(test_file)
sprat_add_test(test_api)
sprat_add_test(test_bind)
sprat_add_test(test_drawing)

# The example prints the fixture, and reports a bad file on stderr with exit code 1.
if(TARGET sprat-dump)
    add_test(NAME sprat_dump COMMAND sprat-dump ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/multipack.sprat)
    set_tests_properties(sprat_dump PROPERTIES PASS_REGULAR_EXPRESSION
        "sprite 3: run_03 \\[atlas 0\\] 0,0 16x32 source 19x35 pivot 4,8 alias of hero/run_01.png")
    # Names with C1 controls and bidirectional marks are printed escaped, other UTF-8 as is.
    add_test(NAME sprat_dump_controls COMMAND sprat-dump ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/controls.sprat)
    set_tests_properties(sprat_dump_controls PROPERTIES PASS_REGULAR_EXPRESSION
        "rlo_\\\\u202Eevil .*alm_\\\\u061Cmark .*rli_\\\\u2067isolate .*c1_\\\\u009B2J .*plain_été ")
    add_test(NAME sprat_dump_error COMMAND sprat-dump ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/README.md)
    set_tests_properties(sprat_dump_error PROPERTIES WILL_FAIL TRUE)
    add_test(NAME sprat_dump_usage COMMAND sprat-dump)
    set_tests_properties(sprat_dump_usage PROPERTIES WILL_FAIL TRUE)
endif()
