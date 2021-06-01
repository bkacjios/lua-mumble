DEPENDENCIES = libssl luajit libprotobuf-c opus

LIBRARIES = $(shell pkg-config --libs $(DEPENDENCIES)) -lev # libev doesn't have a pkg-config file..
INCLUDES = $(shell pkg-config --cflags $(DEPENDENCIES))
CFLAGS = -fPIC -I.

default: all

PROTO_SOURCES	= $(wildcard proto/*.proto)
PROTO_C 		= $(PROTO_SOURCES:.proto=.pb-c.c)

SOURCES			= $(wildcard mumble/*.c)
OBJECTS			= $(PROTO_SOURCES:.proto=.o) $(SOURCES:.c=.o)


# Add optimize flag for normal build
all: CFLAGS += -O2
all: proto $(OBJECTS) mumble.so

# Add debug information for debug build
debug: CFLAGS += -DDEBUG -g
debug: proto $(OBJECTS) mumble.so

proto: $(PROTO_C)

install: all
	cp mumble.so /usr/local/lib/lua/5.1/mumble.so

uninstall:
	rm /usr/local/lib/lua/5.1/mumble.so

clean:
	rm mumble/*.o *.so proto/*.o proto/*.c proto/*.h

proto/%.pb-c.c: proto/%.proto
	protoc-c --c_out=. $<

proto/%.o: proto/%.pb-c.c
	$(CC) -c $(INCLUDES) $(CFLAGS) -o $@ $^

mumble/%.o: mumble/%.c
	$(CC) -c $(INCLUDES) $(CFLAGS) -o $@ $^

mumble.so: $(OBJECTS)
	$(CC) -shared $(CFLAGS) -o $@ $^ $(LIBRARIES)