# Copy this folder to task-NN-short-name/, rename the target, and add
# add_subdirectory(task-NN-short-name) to the root CMakeLists.txt.
add_executable(taskNN src/main.cpp)
target_link_libraries(taskNN PRIVATE gfx_common)
