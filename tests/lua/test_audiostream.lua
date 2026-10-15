local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

local RATE, FRAMES = 22050, 22050

-- Write a 1 second, 16 bit mono WAV file, so the test needs no audio assets
local function u32(n) return string.char(n % 256, math.floor(n / 256) % 256, math.floor(n / 65536) % 256, math.floor(n / 16777216) % 256) end
local function u16(n) return string.char(n % 256, math.floor(n / 256) % 256) end

local path = t.tmpname(".wav")
local file = assert(io.open(path, "wb"))
local data = string.rep(u16(1000), FRAMES)
file:write("RIFF", u32(36 + #data), "WAVE", "fmt ", u32(16), u16(1), u16(1), u32(RATE), u32(RATE * 2), u16(2), u16(16), "data", u32(#data), data)
file:close()

local client = mumble.client()

t.test("open errors", function()
	local stream, err = client:openAudio("/nonexistent.ogg")
	t.eq(stream, nil)
	t.truthy(err:find("failed to open audio file: /nonexistent.ogg", 1, true))
	t.errors("invalid option 'bogus'", client.openAudio, client, path, "bogus")
end)

t.test("info and length", function()
	local stream = assert(client:openAudio(path))
	local info = stream:getInfo()
	t.eq(info.frames, FRAMES)
	t.eq(info.samplerate, RATE)
	t.eq(info.channels, 1)
	t.eq(info.seekable, 1)
	t.eq(stream:getLength("frames"), FRAMES)
	t.eq(stream:getLength("samples"), FRAMES)
	t.eq(stream:getLength("seconds"), 1)
	t.eq(stream:getDuration("frames"), FRAMES)
	t.eq(stream:getTitle(), nil)
	t.eq(stream:getArtist(), nil)
	t.truthy(tostring(stream):find("^mumble.audiostream: "))
end)

t.test("every resampler quality opens", function()
	for _, quality in ipairs({ "best", "medium", "fastest", "zero", "linear" }) do
		t.truthy(client:openAudio(path, quality), quality)
	end
end)

t.test("seek", function()
	local stream = assert(client:openAudio(path))
	t.eq(stream:seek("set", 100), 100)
	t.eq(stream:seek("cur", 10), 110)
	t.eq(stream:seek("end", 0), FRAMES)
	t.eq(stream:seek(), FRAMES)
end)

t.test("volume and looping", function()
	local stream = assert(client:openAudio(path))
	t.eq(stream:getVolume(), 1)
	t.eq(stream:setVolume(0.5), stream)
	t.eq(stream:getVolume(), 0.5)

	t.eq(stream:isLooping(), false)
	t.eq(stream:getLoopCount(), 0)
	t.eq(stream:setLooping(2), stream)
	t.eq(stream:isLooping(), true)
	t.eq(stream:getLoopCount(), 2)
	stream:setLooping(true)
	t.eq(stream:getLoopCount(), math.huge)
	stream:setLooping(false)
	t.eq(stream:isLooping(), false)
	t.errors("number or boolean expected, got string", stream.setLooping, stream, "x")

	stream:fadeTo(0.2, 0.5)
	stream:fadeOut(1)
end)

t.test("play, pause and stop", function()
	local stream = assert(client:openAudio(path))
	t.eq(stream:isPlaying(), false)
	stream:play()
	t.eq(stream:isPlaying(), true)

	local active = 0
	for _, s in pairs(client:getAudioStreams()) do
		if s == stream then active = active + 1 end
	end
	t.eq(active, 1)

	stream:pause()
	t.eq(stream:isPlaying(), false)
	stream:play()
	stream:stop()
	t.eq(stream:isPlaying(), false)
end)

t.test("cleanup", function()
	os.remove(path)
end)

t.run()
