# All tests run headless (offscreen QPA) through ctest.
function(kinetic_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE kinetic_ui Qt6::Test kinetic_warnings)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 900)
endfunction()

# Unit tests
kinetic_test(test_scene)
kinetic_test(test_schema)
kinetic_test(test_undo)
# Physics regression tests
kinetic_test(test_physics)
# Milestone acceptance tests
kinetic_test(test_acceptance_m1)
kinetic_test(test_acceptance_m2)
