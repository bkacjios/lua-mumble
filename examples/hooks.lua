local mumble = require("mumble")

local client = mumble.client()

assert(client:connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))

client:hook("OnConnect", function(client)
	print("OnConnect", client)
	client:auth("Mumble-Bot")
end)

client:hook("OnDisconnect", function(client, reason)
	print("OnDisconnect", reason)
end)

client:hook("OnServerVersion", function(client, event)
	print("OnServerVersion", event)
end)

client:hook("OnServerReject", function(client, event)
	print("OnServerReject", event)
end)

client:hook("OnServerSync", function(client, event)
	print("OnServerSync", event)
end)

client:hook("OnPing", function(client, event)
	print("OnPing", event)
end)

client:hook("OnPongTCP", function(client, event)
	print("OnPongTCP", event)
end)

client:hook("OnPongUDP", function(client, event)
	print("OnPongUDP", event)
end)

client:hook("OnChannelRemove", function(client, channel)
	print("OnChannelRemove", channel)
end)

client:hook("OnChannelState", function(client, event)
	print("OnChannelState", event)
end)

client:hook("OnUserConnect", function(client, event)
	print("OnUserConnect", event)
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

mumble.loop()
