# JNI bridge from SquirrelQR to the shared C QR decoder (libs/sqqr).
cmake_minimum_required(VERSION 3.18.1)
project(sqqr_jni C)

# app/src/main/cpp -> repo root
set(SQQR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../libs/sqqr)

add_library(sqqr_jni SHARED
    sqqr_jni.c
    ${SQQR}/src/sqqr.c
    ${SQQR}/src/sqqr_locate.c
    ${SQQR}/src/sqqr_encode.c
    ${SQQR}/src/sqqr_content.c)
target_include_directories(sqqr_jni PRIVATE ${SQQR}/include)
set_target_properties(sqqr_jni PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(sqqr_jni PRIVATE -Wall -Wextra -O2)
target_link_libraries(sqqr_jni m)
