local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

t.test("rejects bad workers", function()
	t.errors("string or function expected", mumble.thread, 12)
end)

t.test("workers exchange messages", function()
	local thread = mumble.thread(function(worker)
		worker:onMessage(function(w, message)
			w:send("echo:" .. message)
			if message == "buffer" then
				local reply = w:buffer()
				reply:write("from worker buffer")
				w:send(reply)
				w:stop()
			end
		end)
		worker:loop()
	end)

	local received, finished = {}, false
	thread:onMessage(function(controller, message)
		t.eq(controller, thread)
		received[#received + 1] = message
	end)
	thread:onFinish(function(controller)
		finished = true
		mumble.stop()
	end)

	t.eq(thread:send("hi\0there"), thread)
	local buffer = mumble.buffer()
	buffer:write("buffer")
	thread:send(buffer)
	t.truthy(tostring(thread):find("^mumble.thread.controller: "))

	local watchdog = mumble.timer()
	watchdog:start(function() mumble.stop() end, 5)
	mumble.loop()

	t.eq(finished, true)
	t.eq(received[1], "echo:hi\0there")
	t.eq(received[2], "echo:buffer")
	t.eq(received[3], "from worker buffer")
	t.eq(thread:join(), thread)
end)

t.test("workers can't use the main loop", function()
	local thread = mumble.thread(function(worker)
		local mumble = require("mumble")
		local results = {}
		for _, name in ipairs({"client", "timer", "pipe", "thread", "loop", "stop", "getConnections", "getClients"}) do
			local ok, err = pcall(mumble[name])
			results[#results + 1] = name .. "=" .. tostring(not ok and err:find("not available inside a thread worker", 1, true) ~= nil)
		end
		-- Everything else is still there
		results[#results + 1] = "time=" .. type(mumble.time())
		results[#results + 1] = "buffer=" .. tostring(mumble.buffer("x"):read(1) == "x")
		results[#results + 1] = "encoder=" .. tostring(mumble.encoder() ~= nil)
		worker:send(table.concat(results, ","))
	end)

	local received
	thread:onMessage(function(controller, message) received = message end)
	thread:onFinish(function() mumble.stop() end)

	local watchdog = mumble.timer()
	watchdog:start(function() mumble.stop() end, 5)
	mumble.loop()
	thread:join()

	t.eq(received, "client=true,timer=true,pipe=true,thread=true,loop=true,stop=true,getConnections=true,getClients=true,time=number,buffer=true,encoder=true")
end)

t.test("finished workers can't be sent to", function()
	local thread = mumble.thread(function(worker) end)
	t.eq(thread:join(), thread)
	t.errors("has finished", thread.send, thread, "late")
	-- Joining again is harmless
	t.eq(thread:join(), thread)
end)

t.test("workers that fail to load can't be sent to", function()
	local thread = mumble.thread("does-not-exist.lua")
	t.errors("failed to start", thread.send, thread, "hi")
	thread:join()
end)

t.test("finished controllers are collected", function()
	local finished = false
	local thread = mumble.thread(function(worker) end)
	thread:onFinish(function() finished = true end)
	thread:join()
	thread = nil

	-- Let the finish callback run and the handles close, then collect
	local watchdog = mumble.timer()
	watchdog:start(function() mumble.stop() end, 0.1)
	mumble.loop()
	collectgarbage()
	collectgarbage()

	t.eq(finished, true)
end)

t.run()
