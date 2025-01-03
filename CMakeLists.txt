cmake_minimum_required(VERSION 3.10)
project(lua-mumble)

# Set Lua version and dependencies
set(LUAVER "luajit" CACHE STRING "Lua version to compile for (luajit, lua5.1, lua5.2)")
set(LUALIB "/usr/local/lib/lua/5.1" CACHE PATH "Directory to install the Lua module")

# Set dependencies
set(DEPENDENCIES libssl ${LUAVER} libprotobuf-c opus sndfile libuv)

# Include directories and libraries
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(PKG_DEPS REQUIRED ${DEPENDENCIES})

# Compiler flags
set(CMAKE_C_FLAGS "-fPIC -I. -Wall")
if("${LUAVER}" STREQUAL "luajit")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLUAJIT")
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

# C files
file(GLOB SOURCES "mumble/*.c")
set(OBJECTS ${PROTO_C} ${SOURCES})

# Dependencies
include_directories(${PKG_DEPS_INCLUDE_DIRS})
link_directories(${PKG_DEPS_LIBRARY_DIRS})
add_definitions(${PKG_DEPS_CFLAGS_OTHER})

# Proto files compilation step
foreach(proto_file IN LISTS PROTO_SOURCES)
	get_filename_component(proto_name ${proto_file} NAME_WE)
	
	# Ensure the proto files are generated before compiling C files
	add_custom_command(
		OUTPUT "${BUILD_PROTO_DIR}/${proto_name}.pb-c.c" "${BUILD_PROTO_DIR}/${proto_name}.pb-c.h"
		COMMAND protoc-c --c_out=${BUILD_PROTO_DIR} -I${PROTO_PATH} ${proto_file}
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

# Create the library target
add_library(mumble SHARED ${OBJECTS})

# Libraries
target_link_libraries(mumble ${PKG_DEPS_LIBRARIES})

# Installation
install(TARGETS mumble DESTINATION ${LUALIB})

# Uninstallation
add_custom_target(uninstall
	COMMAND rm -f ${LUALIB}/mumble.so
)

# Debug build
set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -g")

# Optimize build
set(CMAKE_C_FLAGS_RELEASE "-O2")
