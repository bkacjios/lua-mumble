local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

t.test("new timers are idle", function()
	local timer = mumble.timer()
	t.eq(timer:isActive(), false)
	t.eq(timer:isPaused(), false)
	t.eq(timer:getCount(), 0)
	t.eq(timer:getDuration(), 0)
	t.eq(timer:getRepeat(), 0)
	t.errors("attempt to call 'pause' on non-repeating mumble.timer", timer.pause, timer)
	t.errors("attempt to call 'again' on non-repeating mumble.timer", timer.again, timer)
	t.truthy(tostring(timer):find("^mumble.timer: "))
end)

t.test("setters", function()
	local timer = mumble.timer()
	t.eq(timer:setDuration(2), timer)
	t.eq(timer:getDuration(), 2)
	t.eq(timer:setRepeat(0.5), timer)
	t.eq(timer:getRepeat(), 0.5)
	timer:set(1, 0.25)
	local after, rep = timer:get()
	t.eq(after, 1)
	t.eq(rep, 0.25)
end)

t.test("stopped timers can't be restarted", function()
	local timer = mumble.timer()
	timer:stop()
	t.errors("on stopped mumble.timer", timer.start, timer, function() end)
end)

t.test("one shot and repeating timers fire", function()
	local fired, ticks, order = 0, 0, {}

	local once = mumble.timer()
	t.eq(once:start(function(self)
		fired = fired + 1
		order[#order + 1] = "once"
	end, 0.01), once)
	t.eq(once:isActive(), true)

	local rep = mumble.timer()
	rep:start(function(self)
		ticks = ticks + 1
		if ticks == 3 then
			t.eq(self:getCount(), 3)
			self:stop()
			order[#order + 1] = "repeat"
			mumble.stop()
		end
	end, 0.02, 0.01)
	t.eq(rep:getRepeat(), 0.01)

	local watchdog = mumble.timer()
	watchdog:start(function() mumble.stop() end, 5)

	mumble.loop()

	t.eq(fired, 1)
	t.eq(ticks, 3)
	t.eq(order[1], "once")
	t.eq(order[2], "repeat")
	t.eq(once:isActive(), false)
end)

t.run()
