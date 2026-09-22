add_library(gfx_camera INTERFACE)
target_include_directories(gfx_camera INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gfx_camera INTERFACE glm::glm)

if(BUILD_TESTING)
    add_executable(camera_tests tests/camera_tests.cpp)
    target_link_libraries(camera_tests PRIVATE gfx_camera)
    target_compile_options(camera_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_options(camera_tests PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<PLATFORM_ID:Windows>>:-static-libstdc++ -static-libgcc>)
    add_test(NAME camera COMMAND camera_tests)
endif()
