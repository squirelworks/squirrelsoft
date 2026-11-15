# JNI bridge from SquirrelGNSS to the desktop positioning core (sqgnss.c).
cmake_minimum_required(VERSION 3.18.1)
project(sqgnss_jni C)

# app/src/main/cpp -> squirrelgnss/
set(SQGNSS ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(sqgnss_jni SHARED
    sqgnss_jni.c
    ${SQGNSS}/src/sqgnss.c)
target_include_directories(sqgnss_jni PRIVATE ${SQGNSS}/include)
set_target_properties(sqgnss_jni PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(sqgnss_jni PRIVATE -Wall -Wextra -O2)
target_link_libraries(sqgnss_jni m)
