local mumble = require("mumble")

local client = assert(mumble.connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))

client:hook("OnConnect", function(client)
	client:auth("Mumble-Bot")
end)

local afk = {
	channel = "AFK",	-- The name of the channel to move the user to
	warning = 10,		-- 10 minute warning before moving a user
	movetime = 90,		-- 1 hour and 30 minutes before moving them to the AFK channel
	message = "You have been idle for %i minutes..</br>You will be moved to <i>%s</i> in %i minutes!",
}

function afk.checkStats(client, event)
	local user = event.user
	local afkchannel = client:getChannel(afk.channel)

	-- Ignore people in the AFK channel
	if not event.idlesecs or not afkchannel or user.channel == afkchannel then return end

	if event.idlesecs > (afk.movetime * 60) - (afk.warning * 60) then
		if not user.warned then
			local idletime = math.floor(event.idlesecs/60)
			local message = afk.message:format(idletime, afkchannel:getName(), afk.movetime - idletime)
			user:message(message)
			user.warned = true
			print(("[AFK] %s has been warned they are AFK"):format(user:getName()))
		end
	elseif user.warned then
		user.warned = false
		print(("[AFK] %s is no longer AFK"):format(user:getName()))
	end

	if event.idlesecs > afk.movetime * 60 then
		user:move(afkchannel)
		print(("[AFK] %s was moved to %s"):format(user:getName(), afkchannel:getName()))
	end
end

function afk.queryUsers(client)
	for k,user in pairs(client:getUsers()) do
		if user ~= client.me then
			user:requestStats()
		end
	end
end

client:hook("OnUserStats", "AFK Check", afk.checkStats)
client:hook("OnPingTCP", "AFK Query Users", afk.queryUsers)

mumble.loop()