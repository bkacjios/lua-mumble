local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

t.test("rejects paths that aren't fifos", function()
	local path = t.tmpname()
	local file = assert(io.open(path, "w"))
	file:close()
	local pipe, err = mumble.pipe(path, function() end)
	os.remove(path)
	t.eq(pipe, nil)
	t.eq(err, "failed to open pipe: file is not an FIFO file")
end)

t.test("reads data written to the fifo", function()
	local path = t.tmpname()
	local received = {}
	local pipe = assert(mumble.pipe(path, function(data)
		received[#received + 1] = data
		if #received == 2 then mumble.stop() end
	end))
	t.truthy(tostring(pipe):find("^mumble.pipe: "))

	-- Write twice, the pipe should reopen itself after each writer closes
	local writes = 0
	local writer = mumble.timer()
	writer:start(function(self)
		writes = writes + 1
		local fifo = assert(io.open(path, "w"))
		fifo:write("message " .. writes)
		fifo:close()
		if writes == 2 then self:stop() end
	end, 0.05, 0.1)

	local watchdog = mumble.timer()
	watchdog:start(function() mumble.stop() end, 5)
	mumble.loop()

	pipe:close()
	os.remove(path)
	t.eq(received[1], "message 1")
	t.eq(received[2], "message 2")
end)

t.run()
