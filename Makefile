# Change what version of Lua we want to compile for
# lua5.1, lua5.2, luajit

ifndef LUAVER
LUAVER = luajit
endif

# Directory to install our module

ifndef LUALIB
LUALIB = /usr/local/lib/lua/5.1
endif

DEPENDENCIES = libssl $(LUAVER) libprotobuf-c opus sndfile libuv

LIBRARIES = $(shell pkg-config --libs $(DEPENDENCIES))
INCLUDES = $(shell pkg-config --cflags $(DEPENDENCIES))
CFLAGS = -fPIC -I. -Wall

ifneq (,$(findstring luajit,$(LUAVER)))
	CFLAGS += -DLUAJIT
endif

default: all

BUILD_DIR = ./build

PROTO_SOURCES	= $(wildcard proto/*.proto)
PROTO_C			= $(PROTO_SOURCES:.proto=.pb-c.c)
PROTO_H			= $(PROTO_SOURCES:.proto=.pb-c.h)
PROTO_BUILT		= $(wildcard proto/*.pb-c.*)

SOURCES			= $(wildcard mumble/*.c)
OBJECTS			= $(PROTO_SOURCES:%.proto=$(BUILD_DIR)/%.o) $(SOURCES:%.c=$(BUILD_DIR)/%.o)

DEPS := $(OBJECTS:.o=.d)

-include $(DEPS)

objects: gitversion.h proto $(OBJECTS) mumble.so

# Add optimize flag for normal build
all: CFLAGS += -O2
all: objects

# Add debug information for debug build
debug: CFLAGS += -DDEBUG -g
debug: objects

gitversion.h: .git/HEAD .git/index
	@echo "#define GIT_VERSION \"$(shell git rev-parse --short HEAD)\"" > $@

# Create a dependency chain to ensure proto files are generated before C files are compiled
proto: $(PROTO_C)

install: all
	mkdir -p $(LUALIB)
	cp mumble.so $(LUALIB)/mumble.so

uninstall:
	rm $(LUALIB)/mumble.so

clean:
	rm -f $(OBJECTS) $(DEPS) $(PROTO_BUILT) gitversion.h

# Generate .pb-c.c and .pb-c.h from .proto files using protoc-c
proto/%.pb-c.c proto/%.pb-c.h: proto/%.proto
	protoc-c --c_out=. $<

# Ensure protobuf header generation completes before compiling C files
$(BUILD_DIR)/proto/%.o: proto/%.pb-c.c
	@mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

$(BUILD_DIR)/mumble/%.o: mumble/%.c | proto
	@mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

mumble.so: $(OBJECTS)
	$(CC) -shared -o $@ $^ $(LIBRARIES)
