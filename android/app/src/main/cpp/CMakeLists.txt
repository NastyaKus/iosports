cmake_minimum_required(VERSION 3.22)
project(iosrt C ASM)
if(NOT IOSRT_ROOT)
  get_filename_component(IOSRT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
endif()
set(RT ${IOSRT_ROOT}/runtime/src)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(GLOB DEFS ${RT}/exports/*.def)
add_custom_command(OUTPUT ${GEN}/decls.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
  COMMAND ${Python3_EXECUTABLE} -I ${IOSRT_ROOT}/tools/gen_decls.py ${RT}/exports ${GEN}/decls.h
  DEPENDS ${DEFS} ${IOSRT_ROOT}/tools/gen_decls.py)
file(GLOB_RECURSE RT_SOURCES ${RT}/*.c)
add_library(iosrt SHARED ${RT_SOURCES} ${RT}/abi/darwin_abi.S ${GEN}/decls.h)
target_include_directories(iosrt PRIVATE ${GEN} ${RT}/exports)
target_compile_options(iosrt PRIVATE -std=gnu11 -fsigned-char -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(iosrt log aaudio dl m)
