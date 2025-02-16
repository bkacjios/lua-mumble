local mumble = require("mumble")
local lfs = require("lfs") -- https://github.com/lunarmodules/luafilesystem

local client = mumble.client()

assert(client:connect("raspberrypi.lan", 64738, "bot.pem", "bot.key"))

client:hook("OnConnect", function(client)
	client:auth("Music-Bot")
end)

local track = 1
local playlist = {}

for file in lfs.dir("music") do
	-- Get all files in our music directory
	if file ~= "." and file ~= ".." then
		-- Open the audio file and add it to our playlist
		local stream = assert(client:openAudio(string.format("music/%s", file)))
		table.insert(playlist, {name = file, stream = stream})
	end
	-- Sort by file name so we play in a deterministic order
	table.sort(playlist, function(a,b) return a.name < b.name end)
end

client:hook("OnAudioStreamEnd", function(client, stream)
	-- Our currently playing track completed
	track = (track % #playlist) + 1
	local song = playlist[track]
	print(string.format("Now playing track #%d - %s", track, song.name))
	song.stream:play()
end)

client:hook("OnServerSync", function(client, event)
	if #playlist >= track then
		-- Play the first track
		playlist[track].stream:play()
	end
end)