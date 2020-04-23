LIBRARIES = $(shell pkg-config --libs libssl luajit libprotobuf-c opus vorbis vorbisfile) -lev
INCLUDES = $(shell pkg-config --cflags libssl luajit libprotobuf-c opus vorbis vorbisfile) -lev
CFLAGS = -fPIC -g -DDEBUG -I.

default: all

clean:
	rm *.o *.so proto/*.o proto/*.c proto/*.h

uninstall:
	rm /usr/local/lib/lua/5.1/mumble.so

PROTO_SOURCES	= $(wildcard proto/*.proto)
PROTO_C 		= $(PROTO_SOURCES:.proto=.pb-c.c)

SOURCES			= $(wildcard *.c)
OBJECTS			= $(PROTO_SOURCES:.proto=.o) $(SOURCES:.c=.o)

all: proto $(OBJECTS) mumble.so

proto: $(PROTO_C)

install: all
	cp mumble.so /usr/local/lib/lua/5.1/mumble.so

proto/%.pb-c.c: proto/%.proto
	protoc-c --c_out=. $<

proto/%.o: proto/%.pb-c.c
	$(CC) -c $(INCLUDES) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) -c $(INCLUDES) $(CFLAGS) -o $@ $^

mumble.so: $(OBJECTS)
	$(CC) -shared $(CFLAGS) -o $@ $^ $(LIBRARIES)