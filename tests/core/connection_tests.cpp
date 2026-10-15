// Tests for connecting, disconnecting and reconnecting a client against real sockets

#include "fixture.h"

#include <arpa/inet.h>
#include <atomic>
#include <netinet/in.h>
#include <openssl/pem.h>

// A Lua state with the module loaded, and a certificate the client can connect with
class ConnectionFixture {
public:
	ConnectionFixture() {
		static EVP_PKEY *pkey = nullptr;
		static X509 *cert = nullptr;
		if (!pkey) make_certificate(&pkey, &cert);
		this->pkey = pkey;
		this->cert = cert;

		certificate = temp_file();
		key = temp_file();
		FILE *file = fopen(certificate.c_str(), "w");
		PEM_write_X509(file, cert);
		fclose(file);
		file = fopen(key.c_str(), "w");
		PEM_write_PrivateKey(file, pkey, NULL, NULL, 0, NULL, NULL);
		fclose(file);

		L = luaL_newstate();
		luaL_openlibs(L);
		lua_pushcfunction(L, luaopen_mumble);
		lua_call(L, 0, 1);
		lua_setglobal(L, "mumble");

		lua("CERT = '" + certificate + "' KEY = '" + key + "'");
	}

	~ConnectionFixture() {
		lua_close(L);
		// Let anything the client closed finish closing
		uv_run(uv_default_loop(), UV_RUN_NOWAIT);
		unlink(certificate.c_str());
		unlink(key.c_str());
	}

	void lua(const std::string &code) {
		if (luaL_dostring(L, code.c_str()) != 0) {
			fprintf(stderr, "  lua error: %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
			CHECK(false);
		}
	}

	double number(const std::string &name) {
		lua_getglobal(L, name.c_str());
		double value = lua_tonumber(L, -1);
		lua_pop(L, 1);
		return value;
	}

	// Run the event loop until the Lua global `done` is set, or we give up
	bool runUntilDone(double seconds) {
		auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
		while (std::chrono::steady_clock::now() < end) {
			uv_run(uv_default_loop(), UV_RUN_NOWAIT);
			lua_getglobal(L, "done");
			bool done = lua_toboolean(L, -1);
			lua_pop(L, 1);
			if (done) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	lua_State *L;
	EVP_PKEY *pkey;
	X509 *cert;
	std::string certificate;
	std::string key;

private:
	static std::string temp_file() {
		char path[] = "/tmp/mumble-connection-test-XXXXXX";
		close(mkstemp(path));
		return path;
	}
};

// A port on localhost that refuses connections
static int refused_port() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bind(fd, (sockaddr*) &address, sizeof(address));
	socklen_t length = sizeof(address);
	getsockname(fd, (sockaddr*) &address, &length);
	close(fd);
	return ntohs(address.sin_port);
}

// A TLS server that completes the handshake, then drops each connection, as if it was lost
class DroppingServer {
public:
	DroppingServer(EVP_PKEY *pkey, X509 *cert, int connections) {
		context = SSL_CTX_new(TLS_server_method());
		SSL_CTX_use_certificate(context, cert);
		SSL_CTX_use_PrivateKey(context, pkey);

		listener = socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		bind(listener, (sockaddr*) &address, sizeof(address));
		listen(listener, 4);
		socklen_t length = sizeof(address);
		getsockname(listener, (sockaddr*) &address, &length);
		port = ntohs(address.sin_port);

		thread = std::thread([this, connections]() {
			for (int i = 0; i < connections; i++) {
				int fd = accept(listener, NULL, NULL);
				if (fd < 0) return;
				SSL *ssl = SSL_new(context);
				SSL_set_fd(ssl, fd);
				if (SSL_accept(ssl) == 1) accepted++;
				SSL_free(ssl);
				close(fd);
			}
		});
	}

	~DroppingServer() {
		shutdown(listener, SHUT_RDWR);
		close(listener);
		thread.join();
		SSL_CTX_free(context);
	}

	int port;
	std::atomic<int> accepted{0};

private:
	SSL_CTX *context;
	int listener;
	std::thread thread;
};

TEST(connection_reconnects_from_disconnect_hook) {
	ConnectionFixture f;
	f.lua("PORT = " + std::to_string(refused_port()));
	f.lua(R"(
		attempts = 0
		results = {}
		client = mumble.client()
		client:hook('OnDisconnect', function(c, reason)
			attempts = attempts + 1
			if attempts < 3 then
				-- The connection is still closing here, so this waits for it
				results[#results + 1] = c:connect('127.0.0.1', PORT, CERT, KEY)
			else
				done = true
			end
		end)
		assert(client:connect('127.0.0.1', PORT, CERT, KEY))
	)");

	CHECK(f.runUntilDone(5));
	CHECK_EQ(f.number("attempts"), 3.0);
	f.lua("assert(results[1] == true and results[2] == true)");
}

TEST(connection_reconnects_after_being_dropped) {
	ConnectionFixture f;
	DroppingServer server(f.pkey, f.cert, 3);
	f.lua("PORT = " + std::to_string(server.port));
	f.lua(R"(
		connects = 0
		client = mumble.client()
		client:hook('OnConnect', function(c)
			connects = connects + 1
		end)
		client:hook('OnDisconnect', function(c, reason)
			if connects < 3 then
				-- Reconnect later from a timer, like examples/reconnect.lua
				mumble.timer():start(function()
					assert(c:connect('127.0.0.1', PORT, CERT, KEY))
				end, 0.01)
			else
				done = true
			end
		end)
		assert(client:connect('127.0.0.1', PORT, CERT, KEY))
	)");

	CHECK(f.runUntilDone(10));
	CHECK_EQ(f.number("connects"), 3.0);
	CHECK_EQ(server.accepted.load(), 3);
}

TEST(connection_failing_reconnects_report_to_the_hook) {
	ConnectionFixture f;
	f.lua("PORT = " + std::to_string(refused_port()));
	f.lua(R"(
		attempts = 0
		client = mumble.client()
		client:hook('OnDisconnect', function(c, reason)
			attempts = attempts + 1
			if attempts == 1 then
				assert(reason:find('refused'), reason)
			elseif attempts > 1 then
				-- Our queued connect couldn't load its certificate, which is reported here instead
				assert(reason:find('could not load certificate', 1, true), reason)
			end
			if attempts < 50 then
				-- Failing straight away every time must not recurse
				assert(c:connect('127.0.0.1', PORT, '/nonexistent/cert.pem', '/nonexistent/key.pem'))
			else
				done = true
			end
		end)
		assert(client:connect('127.0.0.1', PORT, CERT, KEY))
	)");

	CHECK(f.runUntilDone(5));
	CHECK_EQ(f.number("attempts"), 50.0);
}

TEST(connection_rejects_connecting_twice) {
	ConnectionFixture f;
	f.lua("PORT = " + std::to_string(refused_port()));
	f.lua(R"(
		client = mumble.client()
		assert(client:connect('127.0.0.1', PORT, CERT, KEY))
		local ok, err = client:connect('127.0.0.1', PORT, CERT, KEY)
		assert(ok == false and err == 'already connected', tostring(err))
		client:hook('OnDisconnect', function() done = true end)
	)");
	CHECK(f.runUntilDone(5));
}
