# CMakeLists.txt for NetDebugLink

# Add module to include path
target_include_directories(xr PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# Auto-include source files
file(GLOB MODULE_NETDEBUGLINK_SRC
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

target_sources(xr PRIVATE ${MODULE_NETDEBUGLINK_SRC})

target_compile_definitions(xr PUBLIC LIBXR_DEBUG_BUILD)
