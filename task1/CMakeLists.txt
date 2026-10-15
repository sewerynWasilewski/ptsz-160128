# Plik wykonywalny nazwany numerem indeksu autora: build/task1/160128
add_executable(task1_160128 scheduler.cpp)
set_target_properties(task1_160128 PROPERTIES
    OUTPUT_NAME 160128
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/task1)
