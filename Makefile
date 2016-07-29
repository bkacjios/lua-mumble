CFLAGS = `pkg-config --libs --cflags libssl luajit libprotobuf-c opus vorbis vorbisfile` -pthread -g -DDEBUG

INCLUDES = -I.

default: all

clean:
	rm *.o *.so proto/Mumble.o proto/Mumble.pb-c.c proto/Mumble.pb-c.h

uninstall:
	rm /usr/local/lib/lua/5.1/mumble.so

all: proto/Mumble.o mumble.o client.o user.o channel.o packet.o util.o mumble.so

install: all
	cp mumble.so /usr/local/lib/lua/5.1/mumble.so

proto/Mumble.o: proto/Mumble.proto
	protoc-c --c_out=. $<
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ proto/Mumble.pb-c.c

mumble.o: mumble.c
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ $^ $(CFLAGS)

client.o: client.c
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ $^ $(CFLAGS)

user.o: user.c
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ $^ $(CFLAGS)

channel.o: channel.c
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ $^ $(CFLAGS)

packet.o: packet.c
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ $^ $(CFLAGS)

util.o: util.c
	$(CC) -c $(INCLUDES) -fPIC -shared -o $@ $^ $(CFLAGS)

mumble.so: proto/Mumble.o mumble.o client.o user.o channel.o packet.o util.o
	$(CC) -shared -o $@ $^ $(CFLAGS)