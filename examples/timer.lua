local mumble = require("mumble")

local client = assert(mumble.connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))

client:hook("OnConnect", function(client)
	client:auth("Mumble-Bot")
end)

client:hook("OnServerSync", function(event)
	local i = 0

	local timer = mumble.timer(function(t)
		i = i + 1

		print(i)

		if i >= 3 then
			print("Stopping timer..")
			t:stop()
			client:disconnect()
			mumble.stop()
		end
	end)

	-- A timer that will start after 1 second and repeat every 1 second thereafter
	-- Once our counter hits 3, stop the timer
	timer:start(1, 1)
end)

mumble.loop()

print("Done!")