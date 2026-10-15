# Change what version of Lua we want to compile for
# lua5.1, lua5.2, luajit

ifndef LUAVER
LUAVER = luajit
endif

# Directory to install our module

ifndef LUALIB
LUALIB = /usr/local/lib/lua/5.1
endif

DEPENDENCIES = libssl libcrypto $(LUAVER) libprotobuf-c opus sndfile libuv samplerate

LIBRARIES = $(shell pkg-config --libs $(DEPENDENCIES))
INCLUDES = $(shell pkg-config --cflags $(DEPENDENCIES))
CFLAGS ?= -fPIC -I. -Wall
CXXFLAGS ?= -fPIC -I. -Imumble -Wall -std=c++17

ifneq (,$(findstring luajit,$(LUAVER)))
	CFLAGS += -DLUAJIT
	CXXFLAGS += -DLUAJIT
endif

default: all

BUILD_DIR = ./build

PROTO_SOURCES	= $(wildcard proto/*.proto)
PROTO_C			= $(PROTO_SOURCES:.proto=.pb-c.c)
PROTO_H			= $(PROTO_SOURCES:.proto=.pb-c.h)
PROTO_BUILT		= $(wildcard proto/*.pb-c.*)

SOURCES			= $(wildcard mumble/*.cpp mumble/*/*.cpp)
OBJECTS			= $(PROTO_SOURCES:%.proto=$(BUILD_DIR)/%.o) $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)

DEPS := $(OBJECTS:.o=.d)

-include $(DEPS)

objects: gitversion.h proto $(OBJECTS) mumble.so

# Add optimize flag for normal build
all: CFLAGS += -O2
all: CXXFLAGS += -O2
all: objects

# Add debug information for debug build
debug: CFLAGS += -DDEBUG -g -O0
debug: CXXFLAGS += -DDEBUG -g -O0
debug: objects

gitversion.h: .git/HEAD .git/index
	@echo "#define GIT_VERSION \"$(shell git rev-parse --short HEAD)\"" > $@

# Create a dependency chain to ensure proto files are generated before C++ files are compiled
proto: $(PROTO_C)

install: all
	mkdir -p $(LUALIB)
	cp mumble.so $(LUALIB)/mumble.so

uninstall:
	rm $(LUALIB)/mumble.so

clean:
	rm -f $(OBJECTS) $(DEPS) $(PROTO_BUILT) gitversion.h

# protoc-c is deprecated in favour of protoc with the protoc-gen-c plugin, so use that when we have both
PROTOC = $(shell command -v protoc)
PROTOC_GEN_C = $(shell command -v protoc-gen-c)
ifneq ($(and $(PROTOC),$(PROTOC_GEN_C)),)
PROTOC_C = $(PROTOC) --plugin=protoc-gen-c=$(PROTOC_GEN_C)
else
PROTOC_C = protoc-c
endif

# Generate .pb-c.c and .pb-c.h from .proto files
proto/%.pb-c.c proto/%.pb-c.h: proto/%.proto
	$(PROTOC_C) --c_out=. $<

# Ensure protobuf header generation completes before compiling C++ files
$(BUILD_DIR)/proto/%.o: proto/%.pb-c.c
	@mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

$(BUILD_DIR)/mumble/%.o: mumble/%.cpp | proto
	@mkdir -p $(@D)
	$(CXX) -c $(INCLUDES) $(CXXFLAGS) -MD -o $@ $<

mumble.so: $(OBJECTS)
	$(CXX) -shared -o $@ $^ $(LIBRARIES)
