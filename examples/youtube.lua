-- Stream the audio of a YouTube video with "!yt <link>" or "/yt <link>", and stop it with "!yt stop"
-- Needs yt-dlp and ffmpeg

local mumble = require("mumble")

local client = mumble.client()

assert(client:connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))

client:hook("OnConnect", function(client)
	client:auth("Mumble-Bot")
end)

local stream -- What's playing

-- Only accept YouTube links, so the bot can't be pointed at other sites or local files
local function youtubelink(text)
	-- Mumble sends links as html, so take the href, otherwise the text itself
	local link = text:match('href="([^"]+)"') or text:gsub("<[^>]+>", "")
	link = link:gsub("&amp;", "&"):match("^%s*(.-)%s*$")

	local host = link:match("^https://([%w%.%-]+)/")
	if host == "youtu.be" or host == "youtube.com" or (host and host:match("^[%w%-]+%.youtube%.com$")) then
		return link
	end
end

local function reply(text)
	client:getMe():getChannel():message(text)
end

client:hook("OnMessage", function(client, event)
	local argument = event.message:match("^[!/]yt%s+(.+)$")
	if not argument then return end

	if stream then
		stream:stop()
		stream = nil
	end

	if argument == "stop" then
		reply("Stopped")
		return
	end

	local link = youtubelink(argument)
	if not link then
		reply("That isn't a YouTube link")
		return
	end

	-- yt-dlp downloads the audio, and ffmpeg decodes it to the raw 48kHz stereo floats we play.
	-- No shell is involved, so the link is passed along as is, and "--" stops yt-dlp reading it as an option.
	local err
	stream, err = client:openAudioProcess({
		{ "yt-dlp", "--quiet", "--format", "bestaudio", "--output", "-", "--", link },
		{ "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", "-", "-f", "f32le", "-ar", "48000", "-ac", "2", "-" },
	})
	if not stream then
		reply("Couldn't play that: " .. err)
		return
	end

	stream:play()
	reply("Playing " .. link)
end)

client:hook("OnAudioStreamEnd", function(client, ended)
	if ended == stream then
		stream = nil
	end
end)

mumble.loop()
