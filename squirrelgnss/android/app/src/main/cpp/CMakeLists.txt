# JNI bridge: sqgnss + sqntrip + sqrtcm (same portable cores as desktop).
cmake_minimum_required(VERSION 3.18.1)
project(sqgnss_jni C)

# app/src/main/cpp -> squirrelgnss/
set(SQGNSS ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(sqgnss_jni SHARED
    sqgnss_jni.c
    ${SQGNSS}/src/sqgnss.c
    ${SQGNSS}/src/sqntrip.c
    ${SQGNSS}/src/sqntrip_transport.c
    ${SQGNSS}/src/sqrtcm.c)
target_include_directories(sqgnss_jni PRIVATE ${SQGNSS}/include)
set_target_properties(sqgnss_jni PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(sqgnss_jni PRIVATE -Wall -Wextra -O2)
target_link_libraries(sqgnss_jni m)
