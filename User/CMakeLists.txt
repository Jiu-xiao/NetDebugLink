idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")

set(LIBXR_LOG_LEVEL 3)
include(../libxr/CMake/esp32.cmake)

include(../Modules/CMakeLists.txt)

target_include_directories(
  xr
  PUBLIC $<TARGET_PROPERTY:idf::bt,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC $<TARGET_PROPERTY:idf::mbedtls,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(xr PUBLIC idf::bt idf::mbedtls)
