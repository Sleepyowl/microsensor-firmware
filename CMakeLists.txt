cmake_minimum_required(VERSION 3.20.0)


set(BEE_MANUFACTURER_NAME "BeeEye")
set(BEE_MODEL_ID "MPsensor")


# Generate compile_commands.json for C/C++ Extension
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(microsensor)

# Version object library (no linking overhead)
add_library(version_obj OBJECT src/version.c)
target_link_libraries(version_obj PRIVATE zephyr_interface)

target_compile_definitions(version_obj PRIVATE
    BEE_MANUFACTURER_NAME="${BEE_MANUFACTURER_NAME}"
    BEE_MODEL_ID="${BEE_MODEL_ID}"
)

file(GLOB app_sources CONFIGURE_DEPENDS
    src/main.c        
    src/ble_server.c  
    src/rtc.c                
    src/deep_sleep.c  
    src/rtc_rv3028_addons.c  
    src/vsense.c    
    src/sensor_hdc2080.c     
    src/zigbee_sensor.c
)

target_sources(app PRIVATE ${app_sources})
target_link_libraries(app PRIVATE version_obj)

# Strict compile checks
target_compile_options(app PRIVATE
    -Wall
    -Wextra

    # 1) No implicit function declarations
    -Werror=implicit-function-declaration

    # 2) Non-void function must return on all paths
    -Werror=return-type
)