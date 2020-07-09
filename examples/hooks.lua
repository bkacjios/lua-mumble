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

client:hook("OnClientPing", function(event)
	print("OnClientPing", event)
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

client:hook("OnUserConnected", function(user)
	print("OnUserConnected", user)
end)

client:hook("OnUserRemove", function(event)
	print("OnUserRemove", event)
end)

client:hook("OnUserState", function(event)
	print("OnUserState", event)
end)

client:hook("OnUserChannel", function(event)
	print("OnUserChannel", event)
end)

client:hook("OnUserStartSpeaking", function(user)
	print("OnUserStartSpeaking", user)
end)

client:hook("OnUserSpeak", function(event)
	print("OnUserSpeak", event)
end)

client:hook("OnUserStopSpeaking", function(user)
	print("OnUserStopSpeaking", user)
end)

client:hook("OnMessage", function(event)
	print("OnMessage", event)
end)

client:hook("OnPermissionDenied", function(event)
	print("OnPermissionDenied", event)
end)

client:hook("OnACL", function(event)
	print("OnACL", event)
end)

client:hook("OnPermissionQuery", function(event)
	print("OnPermissionQuery", event)
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

client:hook("OnSuggestConfig", function(event)
	print("OnSuggestConfig", event)
end)

client:hook("OnAudioFinished", function(channel)
	print("OnAudioFinished", channel)
end)

client:hook("OnAudioStreamEnd", function()
	print("OnAudioStreamEnd")
end)

client:hook("OnDisconnect", function()
	print("OnDisconnect")
end)

mumble.loop()