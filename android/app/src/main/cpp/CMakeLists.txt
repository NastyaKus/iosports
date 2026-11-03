cmake_minimum_required(VERSION 3.22)
project(iosrt C CXX ASM)
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
# Apple-ABI C++ support: std::__1 compiled against our __config_site (alternate string layout)
add_library(iosrt_cxx OBJECT ${RT}/cxx/cxx_string.cpp)
target_compile_options(iosrt_cxx PRIVATE -std=c++17 -O2 -fno-exceptions -nostdinc++
  -isystem ${RT}/cxx/include -isystem ${CMAKE_SYSROOT}/usr/include/c++/v1)
add_library(iosrt SHARED ${RT_SOURCES} ${RT}/abi/darwin_abi.S ${GEN}/decls.h $<TARGET_OBJECTS:iosrt_cxx>)
target_include_directories(iosrt PRIVATE ${GEN} ${RT}/exports)
target_compile_definitions(iosrt PRIVATE IOSRT_WITH_CXX IOSRT_WITH_ZLIB)
target_compile_options(iosrt PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu11> -fsigned-char -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(iosrt log aaudio z dl m -static-libstdc++)
