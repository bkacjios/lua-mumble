DEPENDENCIES = libssl luajit libprotobuf-c opus

LIBRARIES = $(shell pkg-config --libs $(DEPENDENCIES)) -lev # libev doesn't have a pkg-config file..
INCLUDES = $(shell pkg-config --cflags $(DEPENDENCIES))
CFLAGS = -fPIC -I.

default: all

BUILD_DIR = ./build

PROTO_SOURCES	= $(wildcard proto/*.proto)
PROTO_C 		= $(PROTO_SOURCES:.proto=.pb-c.c)
PROTO_BUILT		= $(wildcard proto/*.pb-c.*)

SOURCES			= $(wildcard mumble/*.c)
OBJECTS			= $(PROTO_SOURCES:%.proto=$(BUILD_DIR)/%.o) $(SOURCES:%.c=$(BUILD_DIR)/%.o)

DEPS := $(OBJECTS:.o=.d)

-include $(DEPS)

# Add optimize flag for normal build
all: CFLAGS += -O2
all: gitversion.h proto $(OBJECTS) mumble.so

# Add debug information for debug build
debug: CFLAGS += -DDEBUG -g
debug: proto $(OBJECTS) mumble.so

gitversion.h: .git/HEAD .git/index
	echo "#define GIT_VERSION \"$(shell git rev-parse --short HEAD)\"" > $@

proto: $(PROTO_C)

install: all
	cp mumble.so /usr/local/lib/lua/5.1/mumble.so

uninstall:
	rm /usr/local/lib/lua/5.1/mumble.so

clean:
	rm -f $(OBJECTS) $(DEPS) $(PROTO_BUILT) gitversion.h

proto/%.pb-c.c: proto/%.proto
	protoc-c --c_out=. $<

$(BUILD_DIR)/proto/%.o: proto/%.pb-c.c
	mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

$(BUILD_DIR)/mumble/%.o: mumble/%.c
	mkdir -p $(@D)
	$(CC) -c $(INCLUDES) $(CFLAGS) -MD -o $@ $<

mumble.so: $(OBJECTS)
	$(CC) -shared -o $@ $^ $(LIBRARIES)