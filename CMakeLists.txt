cmake_minimum_required(VERSION 3.13)
project(bol-launcher C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
find_package(OpenSSL REQUIRED)

find_program(GLIB_RESOURCE_TOOL glib-compile-resources REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources.c
    COMMAND ${GLIB_RESOURCE_TOOL} --target=${CMAKE_CURRENT_BINARY_DIR}/resources.c --generate-source --sourcedir=${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/launcher.gresource.xml
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/launcher.gresource.xml ${CMAKE_CURRENT_SOURCE_DIR}/ui/launcher.glade
)

add_executable(bol-launcher
    src/main.cpp
    src/settings.cpp
    src/serverlist.cpp
    src/gamelauncher.cpp
    src/updater.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.c
)

target_include_directories(bol-launcher PRIVATE ${GTK3_INCLUDE_DIRS})
target_compile_options(bol-launcher PRIVATE ${GTK3_CFLAGS_OTHER} -Wall)
target_link_libraries(bol-launcher PRIVATE ${GTK3_LIBRARIES} OpenSSL::Crypto pthread)
target_link_directories(bol-launcher PRIVATE ${GTK3_LIBRARY_DIRS})

option(STATIC_CPP "Put the C++ runtime inside the executable" OFF)
if(STATIC_CPP)
    target_link_options(bol-launcher PRIVATE -static-libstdc++ -static-libgcc)
endif()

install(TARGETS bol-launcher DESTINATION bin)
