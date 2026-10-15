local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

local client = mumble.client()

t.test("defaults before connecting", function()
	t.eq(client:isConnected(), false)
	t.eq(client:isSynced(), false)
	t.eq(client:isSynched(), false)
	t.eq(client:getVolume(), 0.5)
	t.eq(client:getAudioPacketSize(), 20)
	t.eq(client:getVoiceTarget(), 0)
	t.eq(client:isTunnelingUDP(), true)
	t.eq(client:isLegacy(), false)
	t.eq(#client:getUsers(), 0)
	t.eq(#client:getChannels(), 0)
	t.eq(type(client:getHooks()), "table")
	t.eq(type(client:getAudioStreams()), "table")
	t.eq(client.me, nil)
	t.eq(client.host, nil)
	t.eq(client.port, nil)
	t.truthy(tostring(client):find("^mumble.client: "))
end)

t.test("version picks legacy mode", function()
	t.eq(mumble.client(1, 4, 0):isLegacy(), true)
	t.eq(mumble.client(1, 5, 0):isLegacy(), false)
	t.eq(mumble.client(2, 0, 0):isLegacy(), false)
end)

t.test("settings", function()
	client:setVolume(0.25)
	client:setVoiceTarget(3)
	client:setAudioPacketSize(40)
	t.eq(client:getVolume(), 0.25)
	t.eq(client:getVoiceTarget(), 3)
	t.eq(client:getAudioPacketSize(), 40)
	client:setVoiceTarget()
	t.eq(client:getVoiceTarget(), 0)
	t.errors("must be one the following values: 10, 20, 40, 60", client.setAudioPacketSize, client, 33)
end)

t.test("methods that need a connection error", function()
	t.errors("on disconnected mumble.client", client.auth, client, "name")
	t.errors("on disconnected mumble.client", client.requestBanList, client)
	t.errors("on connecting mumble.client", client.getPing, client)
	t.errors("on connecting mumble.client", client.disconnect, client)
end)

t.test("hooks are stored by name", function()
	local first = function() end
	local second = function() end
	client:hook("OnTest", first)
	client:hook("OnTest", "second", second)
	t.eq(client:getHooks().OnTest.hook, first)
	t.eq(client:getHooks().OnTest.second, second)
	client:unhook("OnTest", "second")
	t.eq(client:getHooks().OnTest.second, nil)
	client:unhook("OnTest")
	t.eq(client:getHooks().OnTest.hook, nil)
	client:unhook("NoSuchHook")
	t.errors("bad argument #4", client.hook, client, "OnTest", "name", 12)
end)

t.test("call ignores hooks on clients that aren't connected", function()
	client:hook("OnCall", function() return 1 end)
	t.eq(select("#", client:call("OnCall", 1, 2)), 0)
end)

t.test("connect reports bad arguments", function()
	local ok, err = mumble.client():connect("127.0.0.1", 64738, "/nonexistent/cert.pem", "/nonexistent/key.pem")
	t.eq(ok, false)
	t.truthy(err:find("could not load certificate and/or key file", 1, true))
end)

t.test("audio buffers", function()
	local buffer = client:createAudioBuffer(44100, 1)
	t.eq(buffer.length, 0)
	t.eq(buffer.capacity, 23040)
	buffer:writeFloat(0.5)
	t.eq(buffer.length, 4)
end)

t.test("encoder is only available once connecting", function()
	t.errors("on connecting mumble.client", client.getEncoder, client)
end)

t.test("collected clients stop their audio threads", function()
	local function threads()
		local n = 0
		local ls = io.popen("ls /proc/$PPID/task 2>/dev/null")
		for _ in ls:lines() do n = n + 1 end
		ls:close()
		return n
	end
	if threads() == 0 then return end -- No procfs

	collectgarbage()
	local before = threads()
	do local _ = mumble.client() end
	t.eq(threads(), before + 3)
	collectgarbage()
	collectgarbage()
	t.eq(threads(), before)
end)

t.run()
