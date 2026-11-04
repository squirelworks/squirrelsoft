# JNI bridge from SquirrelSuicaAnalysis to the shared C FeliCa core (libs/sqfelica).
cmake_minimum_required(VERSION 3.18.1)
project(sqfelica_jni C)

# app/src/main/cpp -> repo root
set(SQFELICA ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../libs/sqfelica)

add_library(sqfelica_jni SHARED
    sqfelica_jni.c
    ${SQFELICA}/src/sqfelica.c)
target_include_directories(sqfelica_jni PRIVATE ${SQFELICA}/include)
set_target_properties(sqfelica_jni PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(sqfelica_jni PRIVATE -Wall -Wextra)
