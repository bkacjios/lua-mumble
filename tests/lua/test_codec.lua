local mumble = require("mumble")
-- Find the harness next to this file, wherever the test is run from
package.path = (arg[0]:match("^(.*[/\\])") or "./") .. "?.lua;" .. package.path
local t = require("harness")

-- 20ms of stereo 48kHz silence
local FRAME = 960
local pcm16 = string.rep("\0\0\0\0", FRAME)
local pcmf = string.rep("\0\0\0\0\0\0\0\0", FRAME)

t.test("encoder defaults and settings", function()
	local enc = mumble.encoder()
	t.eq(enc:getBitRate(), 128000)
	t.eq(enc:getBitrate(), 128000)
	t.eq(enc:getVBR(), false)
	t.eq(enc:getSampleRate(), 48000)

	enc:setBitRate(64000)
	enc:setComplexity(5)
	enc:setVBR(true)
	enc:setInbandFEC(1)
	enc:setPacketLossPerc(10)
	enc:setDTX(1)
	t.eq(enc:getBitRate(), 64000)
	t.eq(enc:getComplexity(), 5)
	t.eq(enc:getVBR(), true)
	t.eq(enc:getInbandFEC(), 1)
	t.eq(enc:getPacketLossPerc(), 10)
	t.eq(enc:getDTX(), 1)
	enc:reset()
	t.truthy(tostring(enc):find("^mumble.encoder: "))
end)

t.test("encoder rejects bad settings", function()
	t.errors("could not initialize opus encoder", mumble.encoder, 12345, 2)
	t.errors("Failed to set complexity", mumble.encoder().setComplexity, mumble.encoder(), 99)
end)

t.test("decoder defaults", function()
	local dec = mumble.decoder()
	t.eq(dec:getChannels(), 2)
	t.eq(dec:getSampleRate(), 48000)
	t.eq(dec:getPhaseInversionDisabled(), 1)
	t.eq(mumble.decoder(16000, 1):getChannels(), 1)
	t.errors("could not initialize opus decoder", mumble.decoder, 12345, 2)
end)

t.test("encode and decode roundtrip", function()
	local enc, dec = mumble.encoder(), mumble.decoder()
	local frame = enc:encode(2, pcm16)
	t.truthy(#frame > 0)
	t.eq(dec:getNumSamples(frame), FRAME * 2)
	t.eq(#dec:decode(frame), FRAME * 2 * 2)

	local fframe = enc:encodeFloat(2, pcmf)
	t.truthy(#fframe > 0)
	t.truthy(#enc:encode_float(2, pcmf) > 0)
	t.eq(#dec:decodeFloat(fframe), FRAME * 2 * 4)

	t.errors("invalid number of samples", dec.decode, dec, "")
end)

t.test("encoding more input than fits in a packet", function()
	-- 60ms of loud noise at the maximum bitrate, with far more input than a packet can hold
	local enc = mumble.encoder()
	enc:setBitRate(510000)
	local noise = {}
	for i = 1, 2880 * 2 * 2 do noise[i] = string.char(math.random(0, 255)) end
	local frame = enc:encode(6, table.concat(noise))
	t.truthy(#frame > 0 and #frame < 0x1FFF)
	t.errors("opus encoding error", enc.encode, enc, 7, pcm16)
end)

t.run()
