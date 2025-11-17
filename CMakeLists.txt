cmake_minimum_required(VERSION 3.20.0)

# Generate compile_commands.json for C/C++ Extension
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(microsensor)
file(GLOB app_sources CONFIGURE_DEPENDS
     src/*.c
     src/**/*.c      
)

target_sources(app PRIVATE ${app_sources})
