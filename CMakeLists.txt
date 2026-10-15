cmake_minimum_required(VERSION 3.10)
project(lua-mumble C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Lua version and dependencies
set(LUAVER "luajit" CACHE STRING "Lua version to compile for (luajit, lua5.1, lua5.2)")
set(LUALIB "/usr/local/lib/lua/5.1" CACHE PATH "Directory to install the Lua module")

# Set dependencies
set(DEPENDENCIES libssl libcrypto ${LUAVER} libprotobuf-c opus sndfile libuv samplerate)

# Include directories and libraries
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(PKG_DEPS REQUIRED ${DEPENDENCIES})

# Compiler flags, added to any given on the command line
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -I. -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -I. -Wall")
include_directories(${CMAKE_SOURCE_DIR}/mumble)
if("${LUAVER}" STREQUAL "luajit")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLUAJIT")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLUAJIT")
endif()

# Output directories
set(BUILD_DIR "${CMAKE_BINARY_DIR}/build")
set(BUILD_PROTO_DIR "${CMAKE_BINARY_DIR}/proto")
set(PROTO_PATH "${CMAKE_SOURCE_DIR}/proto")

# Ensure the proto output directory exists
file(MAKE_DIRECTORY ${BUILD_PROTO_DIR})

# Proto files
file(GLOB PROTO_SOURCES "proto/*.proto")

# Add the generated proto source files
set(PROTO_C ${PROTO_SOURCES})
set(PROTO_H ${PROTO_SOURCES})

# C++ files
file(GLOB SOURCES "mumble/*.cpp" "mumble/*/*.cpp")
set(OBJECTS ${PROTO_C} ${SOURCES})

# Dependencies
include_directories(${PKG_DEPS_INCLUDE_DIRS})
link_directories(${PKG_DEPS_LIBRARY_DIRS})
add_definitions(${PKG_DEPS_CFLAGS_OTHER})

# protoc-c is deprecated in favour of protoc with the protoc-gen-c plugin, so use that when we have both
find_program(PROTOC protoc)
find_program(PROTOC_GEN_C protoc-gen-c)
if(PROTOC AND PROTOC_GEN_C)
	set(PROTOC_C_COMMAND ${PROTOC} --plugin=protoc-gen-c=${PROTOC_GEN_C})
else()
	set(PROTOC_C_COMMAND protoc-c)
endif()

# Proto files compilation step
foreach(proto_file IN LISTS PROTO_SOURCES)
	get_filename_component(proto_name ${proto_file} NAME_WE)
	
	# Ensure the proto files are generated before compiling C++ files
	add_custom_command(
		OUTPUT "${BUILD_PROTO_DIR}/${proto_name}.pb-c.c" "${BUILD_PROTO_DIR}/${proto_name}.pb-c.h"
		COMMAND ${PROTOC_C_COMMAND} --c_out=${BUILD_PROTO_DIR} -I${PROTO_PATH} ${proto_file}
		DEPENDS ${proto_file}
		COMMENT "Generating C files from proto ${proto_file}"
	)
	list(APPEND OBJECTS "${BUILD_PROTO_DIR}/${proto_name}.pb-c.c")
endforeach()

# Set the GIT_VERSION variable using Git's current commit hash
execute_process(
	COMMAND git rev-parse --short HEAD
	OUTPUT_VARIABLE GIT_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Generate gitversion.h from the current Git commit hash
configure_file(
	${CMAKE_SOURCE_DIR}/gitversion.h.in
	${CMAKE_BINARY_DIR}/gitversion.h
	@ONLY
)

# Add the generated gitversion.h as a dependency for the compilation
include_directories(${CMAKE_BINARY_DIR})

# Lua modules don't need a prefix
set(CMAKE_SHARED_LIBRARY_PREFIX "")

# Compile everything once, so the module and the unit tests can share it
add_library(mumble_objects OBJECT ${OBJECTS})

# Create the library target
add_library(mumble SHARED $<TARGET_OBJECTS:mumble_objects>)

# Libraries
target_link_libraries(mumble ${PKG_DEPS_LIBRARIES})

# Tests
enable_testing()

# C++ unit tests for the core classes
file(GLOB CORE_TESTS "tests/core/*.cpp")
add_executable(core_tests ${CORE_TESTS} $<TARGET_OBJECTS:mumble_objects>)
target_link_libraries(core_tests ${PKG_DEPS_LIBRARIES} Threads::Threads)
add_test(NAME core COMMAND core_tests)

# Lua tests for the module's API, which need an interpreter matching LUAVER
string(REGEX REPLACE "^lua([0-9])\\.?([0-9])$" "lua\\1.\\2" LUA_INTERPRETER_NAME "${LUAVER}")
find_program(LUA_INTERPRETER NAMES ${LUA_INTERPRETER_NAME} ${LUAVER})
if(LUA_INTERPRETER)
	file(GLOB LUA_TESTS "${CMAKE_SOURCE_DIR}/tests/lua/test_*.lua")
	foreach(lua_test IN LISTS LUA_TESTS)
		get_filename_component(lua_test_name ${lua_test} NAME_WE)
		add_test(NAME lua_${lua_test_name} COMMAND ${LUA_INTERPRETER} ${lua_test} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/lua)
		set_tests_properties(lua_${lua_test_name} PROPERTIES ENVIRONMENT "LUA_CPATH=$<TARGET_FILE_DIR:mumble>/?.so;;" TIMEOUT 30)
	endforeach()
else()
	message(STATUS "No Lua interpreter found for ${LUAVER}, skipping Lua tests")
endif()

# Installation
install(TARGETS mumble DESTINATION ${LUALIB})

# Uninstallation
add_custom_target(uninstall
	COMMAND rm -f ${LUALIB}/mumble.so
)

# Debug build
set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -g")
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG -g")

# Optimize build
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
