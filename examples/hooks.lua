local mumble = require("mumble")

local client = assert(mumble.connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))
client:auth("Mumble-Bot")

client:hook("OnServerVersion", function(client, event)
	print("OnServerVersion", event)
end)

client:hook("OnServerReject", function(client, event)
	print("OnServerReject", event)
end)

client:hook("OnServerSync", function(client, event)
	print("OnServerSync", event)
end)

client:hook("OnClientPing", function(client, event)
	print("OnClientPing", event)
end)

client:hook("OnServerPingTCP", function(client, event)
	print("OnServerPingTCP", event)
end)

client:hook("OnServerPingUDP", function(client, event)
	print("OnServerPingUDP", event)
end)

client:hook("OnChannelRemove", function(client, channel)
	print("OnChannelRemove", channel)
end)

client:hook("OnChannelState", function(client, event)
	print("OnChannelState", event)
end)

client:hook("OnUserConnected", function(client, event)
	print("OnUserConnected", event)
end)

client:hook("OnUserRemove", function(client, event)
	print("OnUserRemove", event)
end)

client:hook("OnUserState", function(client, event)
	print("OnUserState", event)
end)

client:hook("OnUserChannel", function(client, event)
	print("OnUserChannel", event)
end)

client:hook("OnUserStartSpeaking", function(client, user)
	print("OnUserStartSpeaking", user)
end)

client:hook("OnUserSpeak", function(client, event)
	print("OnUserSpeak", event)
end)

client:hook("OnUserStopSpeaking", function(client, user)
	print("OnUserStopSpeaking", user)
end)

client:hook("OnMessage", function(client, event)
	print("OnMessage", event)
end)

client:hook("OnPermissionDenied", function(client, event)
	print("OnPermissionDenied", event)
end)

client:hook("OnBanList", function(client, event)
	print("OnBanList", event)
end)

client:hook("OnACL", function(client, event)
	print("OnACL", event)
end)

client:hook("OnCryptSetup", function(client, event)
	print("OnCryptSetup", event)
end)

client:hook("OnUserList", function(client, event)
	print("OnUserList", event)
end)

client:hook("OnPermissionQuery", function(client, event)
	print("OnPermissionQuery", event)
end)

client:hook("OnCodecVersion", function(client, event)
	print("OnCodecVersion", event)
end)

client:hook("OnUserStats", function(client, event)
	print("OnUserStats", event)
end)

client:hook("OnServerConfig", function(client, event)
	print("OnServerConfig", event)
end)

client:hook("OnSuggestConfig", function(client, event)
	print("OnSuggestConfig", event)
end)

client:hook("OnAudioStreamEnd", function(client, stream)
	print("OnAudioStreamEnd", stream)
end)

client:hook("OnDisconnect", function(client, reason)
	print("OnDisconnect", reason)
end)

mumble.loop()