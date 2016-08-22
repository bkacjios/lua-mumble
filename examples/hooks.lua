local mumble = require("mumble")

local client = assert(mumble.connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))
client:auth("Mumble-Bot")

client:hook("OnServerVersion", function(event)
	print("OnServerVersion", event)
end)

client:hook("OnServerReject", function(event)
	print("OnServerReject", event)
end)

client:hook("OnServerSync", function(event)
	print("OnServerSync", event)
end)

client:hook("OnServerPing", function(event)
	print("OnServerPing", event)
end)

client:hook("OnChannelRemove", function(channel)
	print("OnChannelRemove", channel)
end)

client:hook("OnChannelState", function(channel)
	print("OnChannelState", channel)
end)

client:hook("OnUserRemove", function(event)
	print("OnUserRemove", event)
end)

client:hook("OnUserState", function(event)
	print("OnUserState", event)
end)

client:hook("OnMessage", function(event)
	print("OnMessage", event)
end)

client:hook("OnPermissionDenied", function(event)
	print("OnPermissionDenied", event)
end)

client:hook("OnCodecVersion", function(event)
	print("OnCodecVersion", event)
end)

client:hook("OnUserStats", function(event)
	print("OnUserStats", event)
end)

client:hook("OnServerConfig", function(event)
	print("OnServerConfig", event)
end)

client:hook("OnAudioFinished", function()
	print("OnAudioFinished")
end)

while client:isConnected() do
	client:update()
	mumble.sleep(0.01)
end