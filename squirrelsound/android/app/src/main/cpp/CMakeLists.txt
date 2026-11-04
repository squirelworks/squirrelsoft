# JNI bridge from SquirrelSound to the shared C DSP core (libs/sqdsp).
cmake_minimum_required(VERSION 3.18.1)
project(sqdsp_jni C)

# app/src/main/cpp -> repo root
set(SQDSP ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../libs/sqdsp)

add_library(sqdsp_jni SHARED
    sqdsp_jni.c
    ${SQDSP}/src/sqdsp.c)
target_include_directories(sqdsp_jni PRIVATE ${SQDSP}/include)
set_target_properties(sqdsp_jni PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_definitions(sqdsp_jni PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(sqdsp_jni PRIVATE -Wall -Wextra -O2)
target_link_libraries(sqdsp_jni m)
