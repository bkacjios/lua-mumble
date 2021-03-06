local mumble = require("mumble")

local client = assert(mumble.connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))
client:auth("Mumble-Bot")

client:hook("OnServerSync", function(event)
	local timer = mumble.timer()

	local i = 0

	-- A timer that will start after 1 second and repeat every 1 second thereafter
	-- Once our counter hits 3, stop the timer
	timer:start(function(t)
		i = i + 1

		print(i)

		if i >= 3 then
			print("Stopping timer..")
			t:stop()
		end
	end, 1, 1)
end)

mumble.loop()