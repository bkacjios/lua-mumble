local mumble = require("mumble")

local client = mumble.client()

assert(client:connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))

client:hook("OnConnect", function(client)
	client:auth("Mumble-Bot")
end)

client:hook("OnServerSync", function(client, event)
	-- Create a timer
	local timer = mumble.timer()

	-- A timer that will start after 1 second and repeat every 1 second thereafter
	-- Once our counter hits 3, disconnect and break out of the main client loop
	timer:start(function(t)
		print(t:getCount())
		if t:getCount() >= 3 then
			print("Stopping timer..")
			t:stop()
			client:disconnect()
		end
	end, 1, 1)
end)

client:hook("OnDisconnect", function(client, reason)
	mumble.stop()
end)

mumble.loop()

print("Done!")