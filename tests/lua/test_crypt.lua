local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

local function pair()
	local server, client = mumble.crypt(), mumble.crypt()
	server:genKey()
	-- The client encrypts with the server's decrypt IV, and vice versa
	assert(client:setKey(server:getRawKey(), server:getDecryptIV(), server:getEncryptIV()))
	return server, client
end

t.test("new cryptstate is invalid until keyed", function()
	local crypt = mumble.crypt()
	t.eq(crypt:isValid(), false)
	t.truthy(tostring(crypt):find("^mumble.crypt: "))
	crypt:genKey()
	t.eq(crypt:isValid(), true)
	t.eq(#crypt:getRawKey(), 16)
	t.eq(#crypt:getEncryptIV(), 16)
	t.eq(#crypt:getDecryptIV(), 16)
end)

t.test("key setters validate lengths", function()
	local crypt = mumble.crypt()
	t.eq(crypt:setKey("short", string.rep("a", 16), string.rep("b", 16)), false)
	t.eq(crypt:setRawKey("short"), false)
	t.eq(crypt:setRawKey(string.rep("k", 16)), true)
	t.eq(crypt:setEncryptIV(string.rep("e", 16)), true)
	t.eq(crypt:setDecryptIV(string.rep("d", 16)), true)
	t.eq(crypt:getRawKey(), string.rep("k", 16))
	t.eq(crypt:getEncryptIV(), string.rep("e", 16))
	t.eq(crypt:getDecryptIV(), string.rep("d", 16))
	t.eq(crypt:isValid(), false)
end)

t.test("encrypts and decrypts in both directions", function()
	local server, client = pair()
	for _, message in ipairs({ "", "Hello world!", string.rep("x", 100), "nul\0inside" }) do
		local encrypted = assert(client:encrypt(message))
		t.eq(#encrypted, #message + 4)
		t.eq(server:decrypt(encrypted), message)

		encrypted = assert(server:encrypt(message))
		t.eq(client:decrypt(encrypted), message)
	end
	t.eq(server:getGood(), 4)
	t.eq(client:getGood(), 4)
end)

t.test("rejects tampered and replayed packets", function()
	local server, client = pair()
	local encrypted = client:encrypt("payload")
	local tampered = encrypted:sub(1, 5) .. string.char((encrypted:byte(6) + 1) % 256) .. encrypted:sub(7)
	t.eq(server:decrypt(tampered), nil)
	t.eq(server:decrypt(encrypted), "payload")
	t.eq(server:decrypt(encrypted), nil)
end)

t.test("counts lost and late packets", function()
	-- Fixed IVs, since a random IV with a zero second byte looks like a replay to an out of order packet
	local server, client = mumble.crypt(), mumble.crypt()
	local key, a, b = string.rep("k", 16), string.rep("a", 16), string.rep("b", 16)
	assert(server:setKey(key, a, b))
	assert(client:setKey(key, b, a))
	local packets = {}
	for i = 1, 4 do packets[i] = client:encrypt("p" .. i) end
	t.eq(server:decrypt(packets[1]), "p1")
	t.eq(server:decrypt(packets[4]), "p4")
	t.eq(server:getLost(), 2)
	t.eq(server:decrypt(packets[2]), "p2")
	t.eq(server:getLate(), 1)
	t.eq(server:getLost(), 1)
end)

t.run()
