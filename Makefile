# Change what version of Lua we want to compile for
# lua5.1, lua5.2, lua5.3, luajit
LUAVER = luajit

# Directory to install our module
LUALIB = /usr/local/lib/lua/5.1

DEPENDENCIES = libssl $(LUAVER) libprotobuf-c opus

LIBRARIES = $(shell pkg-config --libs $(DEPENDENCIES)) -lev # libev doesn't have a pkg-config file..
INCLUDES = $(shell pkg-config --cflags $(DEPENDENCIES))
CFLAGS = -fPIC -I.

ifneq (,$(findstring luajit,$(LUAVER)))
	CFLAGS += -DLUAJIT
endif

default: all

BUILD_DIR = ./build

PROTO_SOURCES	= $(wildcard proto/*.proto)
PROTO_C 		= $(PROTO_SOURCES:.proto=.pb-c.c)
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

proto: $(PROTO_C)

install: all
	mkdir -p $(LUALIB)
	cp mumble.so $(LUALIB)/mumble.so

uninstall:
	rm $(LUALIB)/mumble.so

clean:
	rm -f $(OBJECTS) $(DEPS) $(PROTO_BUILT) gitversion.h

proto/%.pb-c.c: proto/%.proto
	protoc-c --c_out=. $<

$(BUILD_DIR)/proto/%.o: proto/%.pb-c.c
	@mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

$(BUILD_DIR)/mumble/%.o: mumble/%.c
	@mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

mumble.so: $(OBJECTS)
	$(CC) -shared -o $@ $^ $(LIBRARIES)