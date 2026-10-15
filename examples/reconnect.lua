local mumble = require("mumble")

local HOST, PORT = "raspberrypi.lan", 64738
local CERTIFICATE, KEY = "bot.pem", "bot.key"
local USERNAME = "Mumble-Bot"

-- Wait a little longer after every failed attempt, up to a minute
local MIN_DELAY, MAX_DELAY = 1, 60
local delay = MIN_DELAY

-- Rejections worth trying again, since they can clear up on their own.
-- UsernameInUse is common right after losing connection, while the server still holds our old session.
local RETRY_REJECTS = {
	UsernameInUse = true,
	ServerFull = true,
}

local client = mumble.client()
local giveup = false

local function connect()
	local connecting, err = client:connect(HOST, PORT, CERTIFICATE, KEY)
	if not connecting then
		print(("Failed to connect (%s)"):format(err))
		return false
	end
	return true
end

local function reconnect(reason)
	if giveup then
		print(("Disconnected (%s), not reconnecting"):format(reason))
		mumble.stop()
		return
	end

	print(("Disconnected (%s), reconnecting in %d seconds.."):format(reason, delay))

	-- A timer closes once it has fired, so use a new one for every attempt
	mumble.timer():start(function()
		if not connect() then
			-- Couldn't even start, so try again later
			reconnect("connect failed")
		end
	end, delay)
	delay = math.min(delay * 2, MAX_DELAY)
end

client:hook("OnConnect", function(client)
	client:auth(USERNAME)
end)

client:hook("OnServerSync", function(client, event)
	print("Connected as " .. event.user:getName())
	-- We made it, so start over with short delays next time
	delay = MIN_DELAY
end)

client:hook("OnServerReject", function(client, event)
	local name = mumble.reject[event.type]
	print(("Rejected by server: %s (%s)"):format(tostring(name), tostring(event.reason)))
	if not RETRY_REJECTS[name] then
		giveup = true
	end
end)

-- Called for every way a connection ends: failing to connect, timing out, being kicked or rejected,
-- and calling client:disconnect() ourselves. Set giveup first when disconnecting on purpose.
client:hook("OnDisconnect", function(client, reason)
	reconnect(reason)
end)

if not connect() then
	reconnect("connect failed")
end

mumble.loop()
