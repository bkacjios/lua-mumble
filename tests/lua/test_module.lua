local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

t.test("exposes constructors and metatables", function()
	for _, name in ipairs({ "client", "user", "channel", "encoder", "decoder", "banentry", "voicetarget",
		"timer", "audiostream", "buffer", "pipe", "crypt", "thread" }) do
		t.eq(type(mumble[name]), "table", name)
	end
	t.eq(type(mumble.thread.worker), "table")
	t.eq(type(mumble.thread.controller), "table")
	t.eq(type(mumble.registry), "table")
end)

t.test("time and connections", function()
	t.eq(type(mumble.getTime()), "number")
	t.eq(type(mumble.time()), "number")
	t.eq(type(mumble.gettime()), "number")
	t.eq(type(mumble.getConnections()), "table")
	t.eq(type(mumble.getClients()), "table")
end)

t.test("acl constants", function()
	t.eq(mumble.acl.NONE, 0)
	t.eq(mumble.acl.WRITE, 0x1)
	t.eq(mumble.acl.SPEAK, 0x8)
	t.eq(mumble.acl.LISTEN, 0x800)
	t.eq(mumble.acl.KICK, 0x10000)
	t.eq(mumble.acl.RESET_USER_CONTENT, 0x100000)
	t.eq(mumble.acl.CACHED, 0x8000000)
	t.eq(mumble.acl.ALL, 0x1f0fff)
end)

t.test("audio, codec and log constants", function()
	t.eq(mumble.audio.TINY, 1)
	t.eq(mumble.audio.LARGE, 4)
	t.eq(mumble.codec.OPUS, 4)
	t.eq(mumble.codec.SPEEX, 2)
	t.eq(mumble.codec.CELT_ALPHA, 0)
	t.eq(mumble.codec.UCELT_BETA, 3)
	t.eq(mumble.log.INFO, 1)
	t.eq(mumble.log.ERROR, 3)
	t.eq(mumble.log.TRACE, 5)
	for _, fn in ipairs({ "trace", "debug", "info", "warn", "error", "setLogFile" }) do
		t.eq(type(mumble.log[fn]), "function", fn)
	end
end)

t.test("reject and deny enums map ids to names", function()
	t.eq(mumble.reject[0], "None")
	t.eq(mumble.reject[4], "WrongServerPW")
	t.eq(mumble.reject[5], "UsernameInUse")
	t.eq(mumble.deny[1], "Permission")
	t.eq(mumble.deny[3], "ChannelName")
end)

t.test("log functions format their arguments", function()
	mumble.log.error("formatted %d %s", 42, "ok")
	t.errors("bad argument", mumble.log.info, 12)
end)

t.run()
