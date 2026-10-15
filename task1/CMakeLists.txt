# wszystko laduje w build/task1: 160128 (algorytm), generator, verifier
add_executable(task1_160128 scheduler.cpp)
add_executable(task1_generator generator.cpp)
add_executable(task1_verifier verifier.cpp)

set_target_properties(task1_160128 PROPERTIES OUTPUT_NAME 160128)
set_target_properties(task1_generator PROPERTIES OUTPUT_NAME generator)
set_target_properties(task1_verifier PROPERTIES OUTPUT_NAME verifier)
set_target_properties(task1_160128 task1_generator task1_verifier PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/task1)
