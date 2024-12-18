# lua-mumble

A lua module to connect to a mumble server and interact with it

## Build requirements

```
sudo apt-get install libluajit-5.1-dev protobuf-c libprotobuf-c-dev libssl-dev libopus-dev libev-dev
```

Note: `libluajit-5.1-dev` can be substituted with `liblua5.1-0-dev`, `liblua5.2-dev`, or `liblua5.3-dev` depending on your needs.

## Make usage

```bash
# Removes all object files, generated protobuf c/h files, dependency files, and mumble.so
make clean

# Makes everything
make all

# Make everything with debug flags for use with a debugger
make debug

# Makes only the protobuf c files in the ./proto folder
make proto

# Copies mumble.so in /usr/local/lib/lua/5.1/
make install

# Removes mumble.so in /usr/local/lib/lua/5.1/
make uninstall
```

## Scripting documentation

### mumble

``` lua
-- The mumble library is returned by a require call
local mumble = require("mumble")

-- Create a new mumble client
mumble.client = mumble.client()

-- Main event loop that handles all events, ping, and audio processing
-- This will block the script until SIGINT or mumble.stop(), so call this *after* you create your hooks
mumble.loop()

-- Break out of the mumble.loop() call
mumble.stop()

-- The client's user
-- Is only available *after* "OnServerSync" is called
mumble.user = mumble.client.me
mumble.user = mumble.client:getMe()
mumble.user = mumble.client:getSelf()

-- Run a script in a thread with its own separate Lua environment
mumble.thread = mumble.thread("filename.lua")

-- A new timer object
-- The timer itself will do a best-effort at avoiding drift, that is, if you configure a timer to trigger every 10 seconds, then it will normally trigger at exactly 10 second intervals. If, however, your program cannot keep up with the timer (because it takes longer than those 10 seconds to do stuff) the timer will not fire more than once per event loop iteration.
-- Timers will keep the reference active until mumble.timer:close() is called.
-- Timers will dereference themselves if the timer is stopped after the callback funciton call.
mumble.timer = mumble.timer(Function callback(mumble.timer), Number after = 0, Number repeat = 0)

-- A new voicetarget object
mumble.voicetarget = mumble.voicetarget()

-- A new opus encoder object
-- Sample rate defaults to 48000
mumble.encoder = mumble.encoder(Number samplerate = 48000, Number channels)

-- A new opus decoder object
-- Sample rate defaults to 48000
mumble.decoder = mumble.decoder(Number samplerate = 48000, Number channels)

-- A timestamp in milliseconds
Number time = mumble.getTime()

-- A table of all currect clients
Table clients = mumble.getClients()

-- Structure
Table clients = {
	mumble.client,
	mumble.client,
	...
}
```

### mumble.client

``` lua
-- Begins to connect to a mumble server.
-- Returns true or false depending if we could start connecting or not.
-- If connected is false, an error string will also be returned.
-- Since this method is non-blocking, you can use the "OnConnect" hook to determine when the client has fully connected.
Boolean connecting, [ String error ] = mumble.client:connect(String host, Number port, String certificate file path, String key file path)

-- Authenticate as a user.
-- Should be called inside an "OnConnect" hook.
mumble.client:auth(String username, [ String password, Table tokens ])

-- Manually call a custom hook.
-- Returns whatever the first hook that responded returned.
Varargs ... = mumble.client:call(String hook, ...)

-- Set the bots access tokens
mumble.client:setTokens(Table tokens)

-- Check if the client is connected
Boolean connected = mumble.client:isConnected()

-- Check if the client has fully synced all users and channels
Boolean synced = mumble.client:isSynced()

-- Request the registered ban list from the server
-- When server responds, it will call the 'OnBanList' hook
mumble.client:requestBanList()

-- Request the registered user list from the server
-- When server responds, it will call the 'OnUserList' hook
mumble.client:requestUserList()

-- Disconnect from the connected server
mumble.client:disconnect()

-- Transmit a plugin data packet to a table of users
mumble.client:sendPluginData(String dataID, String plugindata, Table {mumble.user, ...})

-- Transmit a raw, encoded, opus packet
-- Set speaking to false at the end of a stream
mumble.client:transmit(Number codec, String encoded_audio_packet, Boolean speaking = true)

-- Play an ogg audio file
-- Changing the stream value will allow you to play multiple audio files at once
-- If audiostream = nil, it will pass along an error string as to why it couldn't play the file
mumble.audiostream audiostream, [ String error ] = mumble.client:play(String ogg file path, Number stream = 1, Number volume = 1.0)

-- Gets the audio stream object given the stream ID
mumble.audiostream audiostream = mumble.client:getAudioStream(Number stream = 1)

-- Gets a table of all currently active audio streams
Table audiostreams = mumble.client:getAudioStreams()

-- Structure
Table audiostreams = {
	[1]	= mumble.audiostream,
	[2]	= mumble.audiostream,
	...
}

-- Sets the size of each audio packet played.
-- The larger the packet size, the less chance of static.
-- Larger  = higher audio latency.
-- Smaller = lower audio latency.

Table mumble.audio = {
	["TINY"]	= 1,
	["SMALL"]	= 2,
	["MEDIUM"]	= 3,
	["LARGE"]	= 4,
}

mumble.client:setAudioPacketSize(Number size = [TINY = 1, SMALL = 2, MEDIUM = 3, LARGE = 4])

-- Returns the current duration of each audio packet
-- Default: mumble.audio.TINY = 1
Number size = mumble.client:getAudioPacketSize()

-- Checks if the client is currently playing an audio file on the specified audio stream
Boolean playing = mumble.client:isPlaying(Number stream = 1)

-- Stops playing the current audio on the specified audio stream
mumble.client:stopPlaying(Number stream = 1)

-- Sets the global volume level
-- Consider this the master volume level
mumble.client:setVolume(Number volume)

-- Gets the global volume level
Number volume = mumble.client:getVolume()

-- Returns if the client is tunneling UDP voice data through the TCP connection
-- This will be true until the first "OnPongUDP" call
Boolean tunneludp = mumble.client:isTunnelingUDP()

-- Attempts to change the bots comment
mumble.client:setComment(String comment)

-- Adds a callback for a specific event
-- If no unique name is passed, it will default to "hook"
mumble.client:hook(String hook, [ String unique name = "hook" ], Function callback(mumble.client))

-- Gets all registered callbacks
Table hooks = mumble.client:getHooks()

-- Structure
Table hooks = {
	["OnServerSync"] = {
		["hook"] = function: 0xffffffff,
		["do stuff on connection"] = function: 0xffffffff,
	},
	["OnPingTCP"] = {
		["hook"] = function: 0xffffffff,
		["do stuff on ping"] = function: 0xffffffff,
	},
	...
}

-- Register a mumble.voicetarget to the server
-- Accepts multiple mumble.voicetarget objects that will all be assigned to the given ID
mumble.client:registerVoiceTarget(Number id, mumble.voicetarget ...)

-- Set the current voice target that mumble.client:play() will abide by
-- Defaults to 0, the default voice target
mumble.client:setVoiceTarget(Number id)

-- Get the current voice target
Number id = mumble.client:getVoiceTarget()

-- Get the encoder object that the internal audio system uses to play .ogg files
mumble.encoder encoder = mumble.client:getEncoder()

-- Get the average ping of the client
Number ping = mumble.client:getPing()

-- Get the uptime of the current client in seconds
Number time = mumble.client:getUpTime()

-- Returns a table of all mumble.users
Table users = mumble.client:getUsers()

-- Structure
-- Key:		index
-- Value:	mumble.user
Table users = {
	[1] = mumble.user,
	[2] = mumble.user,
	[3] = mumble.user,
	...
}

mumble.channel channel = mumble.client:getChannel(String path)

-- Returns a table of all mumble.channels
Table channels = mumble.client:getChannels()

-- Structure
-- Key:		channel id
-- Value:	mumble.channel
Table channels = {
	[id] = mumble.channel,
	[id] = mumble.channel,
	...
}

-- Request a users full texture data blob
-- Server will respond with a "OnUserState" with the requested data filled out
mumble.client:requestTextureBlob(Table {mumble.user, ...})

-- Request a users full comment data blob
-- Server will respond with a "OnUserState" with the requested data filled out
-- After the hook is called, mumble.user:getComment() will also return the full data
mumble.client:requestCommentBlob(Table {mumble.user, ...})

-- Request a channels full description data blob
-- Server will respond with a "OnChannelState" with the requested data filled out
-- After the hook is called, mumble.channel:getDescription() will also return the full data
mumble.client:requestDescriptionBlob(Table {mumble.user, ...})

-- Creates a channel
-- Will be parented to the root channel
mumble.client:createChannel(String name, String description = "", Number position = 0, Boolean temporary = false, Number max_users = 0)
```

### mumble.user

``` lua
-- Sends a text message to a user
mumble.user:message(String host)

-- Attempts to kick a user with an optional reason value
mumble.user:kick([ String reason ])

-- Attempts to ban a user with an optional reason value
mumble.user:ban([ String reason ])

-- Attempts to move a user to a different channel
mumble.user:move(mumble.channel channel)

-- Attempts to mute a user
-- If no boolean is passed, it will default to muting the user
mumble.user:setMuted([ Boolean mute = true ])

-- Attempts to deafen a user
-- If no boolean is passed, it will default to deafening the user
mumble.user:setDeaf([ Boolean deaf = true ])

-- Attempts to register the users name to the server
mumble.user:register()

-- Requests the users information statistics from the server
-- If no boolean is passed, it will default to requesting ALL statistics
mumble.user:requestStats([ Boolean statsonly = false ])

-- Gets the current mumble.client this user is a part of
mumble.client client = mumble.user:getClient()

-- Gets the current session number
Number session = mumble.user:getSession()

-- Gets the name of the user
String name = mumble.user:getName()

-- Gets the channel of the user
mumble.channel channel = mumble.user:getChannel()

-- Gets the registered ID of the user
-- Is 0 for unregistered users
Number userid = mumble.user:getId()

-- Returns if the user is registered or not
Boolean registered = mumble.user:isRegistered()

-- Returns if the user is muted or not
Boolean muted = mumble.user:isMuted()

-- Returns if the user is deaf or not
Boolean deaf = mumble.user:isDeaf()

-- Returns if the user is muted or not
Boolean muted = mumble.user:isSelfMute()

-- Returns if the user is deaf or not
Boolean deaf = mumble.user:isSelfDeaf()

-- Returns if the user is suppressed by the server
Boolean suppressed = mumble.user:isSuppressed()

-- Returns the comment string of the users comment
String comment = mumble.user:getComment()

-- Returns the comments SHA1 hash
String hash = mumble.user:getCommentHash()

-- Returns if the user is speaking or not
Boolean speaking = mumble.user:isSpeaking()

-- Returns if the user is recording or not
Boolean recording = mumble.user:isRecording()

-- Returns if the user is a priority speaker or not
Boolean priority = mumble.user:isPrioritySpeaker()

-- Returns the users avatar as a string of bytes
String texture = mumble.user:getTexture()

-- Returns the users avatar as a SHA1 hash
String hash = mumble.user:getTextureHash()

-- Returns the users username SHA1 hash
String hash = mumble.user:getHash()

-- Sets the users avatar image using a string of bytes
mumble.user:setTexure(String bytes)

-- Adds a channel to the list of channels the user is listening to
mumble.user:listen(mumble.channel ...)

-- Removes a channel from the list of channels the user is listening to
mumble.user:unlisten(mumble.channel ...)

-- Returns a table of all channels the user is currently listening to
Table listens = mumble.user:getListens()

-- Structure
-- Key:		channel id
-- Value:	mumble.channel
Table channels = {
	[id] = mumble.channel,
	[id] = mumble.channel,
	...
}

-- Transmit a plugin data packet to this user
mumble.user:sendPluginData(String dataID, String plugindata)

-- Request a users full texture data blob
-- Server will respond with a "OnUserState" with the requested data filled out
-- After the hook is called, mumble.user:getTexture() will also return the full data
mumble.user:requestTextureBlob()

-- Request a users full comment data blob
-- Server will respond with a "OnUserState" with the requested data filled out
-- After the hook is called, mumble.user:getComment() will also return the full data
mumble.user:requestCommentBlob()
```

### mumble.channel

``` lua
-- Gets a channel relative to the current
mumble.channel channel = mumble.channel(String path)

-- Sends a text message to the entire channel
mumble.channel:message(String message)

-- Attempts to set the channels description
mumble.channel:setDescription(String description)

-- Attempts to remove the channel
mumble.channel:remove()

-- Gets the current mumble.client this channel is a part of
mumble.client client = mumble.channel:getClient()

-- Gets the channels name
String name = mumble.channel:getName()

-- Gets the channel ID
Number id = mumble.channel:getId()

-- Gets the parent channel
-- Returns nil on root channel
mumble.channel channel = mumble.channel:getParent()

-- Returns the channels that are parented to the channel
Table children = mumble.channel:getChildren()

-- Returns the users that are currently within the channel
Table users = mumble.channel:getUsers()

-- Structure
-- Key:		index
-- Value:	mumble.user
Table users = {
	[1] = mumble.user,
	[2] = mumble.user,
	[3] = mumble.user,
	...
}

-- Gets the channels description
String description = mumble.channel:getDescription()

-- Gets the channels description SHA1 hash
String hash = mumble.channel:getDescriptionHash()

-- Returns if the channel is temporary or not
Boolean temporary = mumble.channel:isTemporary()

-- Returns the channels position
Number position = mumble.channel:getPosition()

-- Gets the max number of users allowed in this channel
Number max = mumble.channel:getMaxUsers()

-- Returns a table of all linked channels
Number linked = mumble.channel:getLinked()

-- Attempts to link channel(s)
mumble.channel:link(mumble.channel ...)

-- Attempts to unlink channel(s)
mumble.channel:unlink(mumble.channel ...)

-- Returns if the client is restricted from entering the channel
-- NOTE: *Will only work in mumble version 1.4+*
Boolean restricted = mumble.channel:isEnterRestricted()

-- Returns if the client is able to enter the channel
-- NOTE: *Will only work in mumble version 1.4+*
Boolean enter = mumble.channel:canEnter()

-- Request ACL config for the channel
-- When server responds, it will call the 'OnACL' hook
mumble.channel:requestACL()

-- Request permissions for the channel
-- When server responds, it will call the 'OnPermissionQuery' hook
mumble.channel:requestPermissions()

-- Gets the permissions value for the channel
Number permissions = mumble.channel:getPermissions()

-- Gets the permissions value for the channel
Boolean permission = mumble.channel:hasPermission(mumble.acl flag)

-- Request a users full texture data blob
-- Server will respond with a "OnChannelState" with the requested data filled out
-- After the hook is called, mumble.channel:getDescription() will also return the full data
mumble.channel:requestTextureBlob()

-- Creates a channel
-- Will be parented to the channel that this method was called from
mumble.channel:create(String name, String description = "", Number position = 0, Boolean temporary = false, Number max_users = 0)
```

### mumble.timer

``` lua
-- Run the timer
mumble.timer = mumble.timer:start()

-- Retruns if the timer is currently running
Boolean running = mumble.timer:isActive()

-- Configure the timer to trigger after after seconds (fractional and negative values are supported).
-- If repeat is 0., then it will automatically be stopped once the timeout is reached.
-- If it is positive, then the timer will automatically be configured to trigger again repeat seconds later, again, and again, until stopped manually.
mumble.timer = mumble.timer:set(Number after, Number repeat = 0)

Number after, repeat = mumble.timer:get()

-- This will act as if the timer timed out, and restarts it again if it is repeating. It basically works like calling mumble.timer.stop, updating the timeout to the repeat value and calling mumble.timer.start.
-- The exact semantics are as in the following rules, all of which will be applied to the watcher:
-- If the timer is pending, the pending status is always cleared.
-- If the timer is started but non-repeating, stop it (as if it timed out, without invoking it).
-- If the timer is repeating, make the repeat value the new timeout and start the timer, if necessary.
mumble.timer:again()

-- Stops the timer
mumble.timer:stop()

-- Closes the timer and marks it for garbage collection
mumble.timer:close()
```

### mumble.voicetarget

``` lua
-- Add a user to whisper to
mumble.voicetarget:addUser(mumble.user user)

-- Return a table of all the users currently in the voicetarget
Table users = mumble.voicetarget:getUsers()

-- Structure
-- Key:		index
-- Value:	Number session
Table channels = {
	Number session,
	Number session,
	...
}

-- Sets the channel that is be shouted to
mumble.voicetarget:setChannel(mumble.channel channel)

-- Gets the channel that is shouted to
mumble.voicetarget:getChannel()

-- Sets the specific user group to whisper to
mumble.voicetarget:setGroup(String group)

-- Gets the group name we are whispering to
String group = mumble.voicetarget:getGroup()

-- Shout to the linked channels of the set channel
mumble.voicetarget:setLinks(Boolean followlinks)

-- Returns if we are currently shouting to linked channels of the set channel
Boolean links = mumble.voicetarget:getLinks()

-- Shout to the children of the set channel
mumble.voicetarget:setChildren(Boolean followchildren)

-- Returns if we are currently shouting to children of the set channel
Boolean children = mumble.voicetarget:getChildren()
```

### mumble.encoder

``` lua
-- Equivalent to OPUS_RESET_STATE
mumble.encoder:reset()

-- Equivalent to OPUS_GET_FINAL_RANGE
Number range = mumble.encoder:getFinalRange()

-- Equivalent to OPUS_GET_BANDWIDTH
Number bandwidth = mumble.encoder:getBandwidth()

-- Equivalent to OPUS_GET_SAMPLE_RATE
Number samplerate = mumble.encoder:getSamplerate()

-- Set the bitrate that the encoder will use
mumble.encoder:setBitRate(Number bitrate)

-- Get the bitrate that the encoder is using
Number bitrate = mumble.encoder:getBitRate()

-- Encode X number of pcm frames into an opus audio packet
String encoded = mumble.encoder:encode(Number frames, String pcm)

-- Encode X number of pcm float frames into an opus audio packet
String encoded = mumble.encoder:encode_float(Number frames, String pcm)
```

### mumble.decoder

``` lua
-- Equivalent to OPUS_RESET_STATE
mumble.decoder:reset()

-- Equivalent to OPUS_GET_FINAL_RANGE
Number range = mumble.decoder:getFinalRange()

-- Equivalent to OPUS_GET_BANDWIDTH
Number bandwidth = mumble.decoder:getBandwidth()

-- Equivalent to OPUS_GET_SAMPLE_RATE
Number samplerate = mumble.decoder:getSamplerate()

-- Set the bitrate that the decoder will use
mumble.decoder:setBitRate(Number bitrate)

-- Get the bitrate that the decoder is using
Number bitrate = mumble.decoder:getBitRate()

-- Decode an opus audio packet into raw PCM data
String decoded = mumble.decoder:decode(String encoded)

-- Decode an opus audio packet into raw PCM float data
String decoded = mumble.decoder:decode_float(String encoded)
```

### mumble.audiostream

``` lua
-- Returns if this audio stream is currently playing or not
Boolean isplaying = mumble.audiostream:isPlaying()

-- Sets the volume of the audio stream
mumble.audiostream:setVolume(Number volume)

-- Gets the volume of the audio stream
Number volume = mumble.audiostream:getVolume()

-- Gets the stream number of this audio stream
Number stream = mumble.audiostream:getVolume()

-- Pause the audio
mumble.audiostream:pause()

-- Resume playing the audio
mumble.audiostream:play()

-- Pauses the audio AND resets playback to the beginning
-- Will remove the stream from the mumble.client:getAudioStreams() table
mumble.audiostream:stop()

-- Will attempt to seek to a given position via sample numbers
-- Returns the offset that it has seeked to
Number samples = mumble.audiostream:seek(String whence ["start", "cur", "end"], Number offset = 0)

-- Returns the duration of the stream given the unit type
Number samples/seconds = mumble.audiostream:getLength(String units ["seconds", "samples"])

-- Returns a table of information about the audio file.
Table info = mumble.audiostream:getInfo()

-- Structure
-- Key:		String
-- Value:	Number
Table info = {
	["channels"] = Number channels,
	["sample_rate"] = Number sample_rate,
	["setup_memory_required"] = Number setup_memory_required,
	["setup_temp_memory_required"] = Number setup_temp_memory_required,
	["temp_memory_required"] = Number temp_memory_required,
	["max_frame_size"] = Number max_frame_size
}

Table comments = mumble.audiostream:getComment()

-- Enables the audio stream to loop to the beginning when reaching the end.
-- Accepts a Boolean value or a Number value.
-- Boolean = true will cause it to loop forever.
-- Number = Will loop X amount of times before eventually stopping.
mumble.audiostream:setLooping([Boolean loop, Number loop_count])

-- Returns if the stream is looping or not
Boolean looping = mumble.audiostream:isLooping()

-- Retuns how many more times the stream will loop before stopping.
-- If you used setLooping(true), this will return math.huge (inf)
Number count = mumble.audiostream:getLoopCount()
```

### mumble.acl

```lua
Table mumble.acl = {
	NONE = 0x0,
	WRITE = 0x1,
	TRAVERSE = 0x2,
	ENTER = 0x4,
	SPEAK = 0x8,
	MUTE_DEAFEN = 0x10,
	MOVE = 0x20,
	MAKE_CHANNEL = 0x40,
	LINK_CHANNEL = 0x80,
	WHISPER = 0x100,
	TEXT_MESSAGE = 0x200,
	MAKE_TEMP_CHANNEL = 0x400,
	LISTEN = 0x800,

	-- Root channel only
	KICK = 0x10000,
	BAN = 0x20000,
	REGISTER = 0x40000,
	SELF_REGISTER = 0x80000,
	RESET_USER_CONTENT = 0x100000,

	CACHED = 0x8000000,
	ALL = WRITE + TRAVERSE + ENTER + SPEAK + MUTE_DEAFEN + MOVE
			+ MAKE_CHANNEL + LINK_CHANNEL + WHISPER + TEXT_MESSAGE + MAKE_TEMP_CHANNEL + LISTEN
			+ KICK + BAN + REGISTER + SELF_REGISTER + RESET_USER_CONTENT,
}
```

### mumble.reject

```lua
Table mumble.reject = {
	[0] = "None",
	[1] = "WrongVersion",
	[2] = "InvalidUsername",
	[3] = "WrongUserPW",
	[4] = "WrongServerPW",
	[5] = "UsernameInUse",
	[6] = "ServerFull",
	[7] = "NoCertificate",
	[8] = "AuthenticatorFail",
}
```

### mumble.deny

```lua
Table mumble.deny = {
	[0]  = "Text",
	[1]  = "Permission",
	[2]  = "SuperUser",
	[3]  = "ChannelName",
	[4]  = "TextTooLong",
	[5]  = "H9K",
	[6]  = "TemporaryChannel",
	[7]  = "MissingCertificate",
	[8]  = "UserName",
	[9]  = "ChannelFull",
	[10] = "NestingLimit",
	[11] = "ChannelCountLimit",
}
```

## hooks

### `OnConnect (mumble.client client)`

Called when the connection to the server is fully established.

### `OnDisconnect (mumble.client client, String reason)`

Called when the connection to the server is disconnected for any reason.

### `OnServerVersion (mumble.client client, Table event)`

Called when the server version information is recieved.

``` lua
Table event = {
	["version"]		= Number version,
	["release"]		= String release,
	["os"]			= String os,
	["os_version"]	= String os_version,
}
```
___

### `OnPongTCP (mumble.client client, Table event)`

Called when the server sends a responce to a TCP ping request.

``` lua
Table event = {
	["ping"]			= Number ping,
	["timestamp"]		= Number timestamp,
	["good"]			= Number good,
	["late"]			= Number late,
	["lost"]			= Number lost,
	["resync"]			= Number resync,
	["udp_packets"]		= Number udp_packets,
	["tcp_packets"]		= Number tcp_packets,
	["udp_ping_avg"]	= Number udp_ping_avg,
	["udp_ping_var"]	= Number udp_ping_var,
	["tcp_ping_avg"]	= Number tcp_ping_avg,
	["tcp_ping_var"]	= Number tcp_ping_var,
}
```
___

### `OnPongUDP (mumble.client client, Table event)`

Called when the server sends a responce to a UDP ping request.

``` lua
Table event = {
	["ping"]			= Number ping,
	["timestamp"]		= Number timestamp,
}
```
___

### `OnServerReject (mumble.client client, Table event)`

Called when you are rejected from connecting to the server.

``` lua
Table event = {
	["type"]	= mumble.reject type,
	["reason"]	= String reason,
}
```
___

### `OnServerSync (mumble.client client, Table event)`

Called after the bot has recieved all the mumble.user and mumble.channel states.

``` lua
Table event = {
	["user"]			= mumble.user user,
	["max_bandwidth"]	= Number max_bandwidth,
	["welcome_text"]	= String welcome_text,
	["permissions"]		= Number permissions,
}
```
___

### `OnChannelRemove (mumble.client client, mumble.channel channel)`

Called when a mumble.channel is removed.
___

### `OnChannelState (mumble.client client, Table event)`

Called when a mumble.channel state has changed.. Like updating the name, description, position, comment, etc..
Not every value will always be set. Only the fields that are currently changing will be set!

``` lua
Table event = {
	["channel"]				= mumble.channel channel,
	["parent"]				= mumble.channel parent,
	["channel_id"]			= Number channel_id,
	["position"]			= Number position,
	["max_users"]			= Number max_users,
	["name"]				= String name,
	["description"]			= String description,
	["description_hash"]	= String description_hash,
	["temporary"]			= Boolean temporary,
	["is_enter_restricted"]	= Boolean is_enter_restricted,
	["can_enter"]			= Boolean can_enter,
	["links"]				= {
		[1] = mumble.channel channel,
		...
	},
	["links_add"]			= {
		[1] = mumble.channel channel,
		...
	},
	["links_remove"]		= {
		[1] = mumble.channel channel,
		...
	}
}
```
___

### `OnUserChannel (mumble.client client, Table event)`

Called when a mumble.user changes their channel

``` lua
Table event = {
	["user"]	= mumble.user user,
	["actor"]	= mumble.user actor,
	["from"]	= mumble.channel from,
	["to"]		= mumble.channel to,
}
```

### `OnUserRemove (mumble.client client, Table event)`

Called when a mumble.user disconnects or is kicked from the server

``` lua
Table event = {
	["user"]	= mumble.user user,
	["actor"]	= mumble.user actor,
	["reason"]	= String reason,
	["ban"]		= Boolean ban,
}
```
___

### `OnUserConnected (mumble.client client, Table event)`

Called when a mumble.user has connected to the server

``` lua
Table event = {
	["user"]	= mumble.user user,
	...
}
```
___

### `OnUserState (mumble.client client, Table event)`

Called when a mumble.user state has changed.. Like updating their comment, moving channels, muted, deafened, etc..
Not every value will always be set. Only the fields that are currently changing will be set!

``` lua
Table event = {
	["user_id"]				= Number user_id,
	["session"]				= Number session,
	["actor"]				= mumble.user actor,
	["user"]				= mumble.user user,
	["channel"]				= mumble.channel channel,
	["mute"]				= Boolean mute,
	["deaf"]				= Boolean deaf,
	["self_mute"]			= Boolean self_mute,
	["self_deaf"]			= Boolean self_deaf,
	["suppress"]			= Boolean suppress,
	["recording"]			= Boolean recording,
	["priority_speaker"]	= Boolean priority_speaker,
	["name"]				= String name,
	["comment"]				= String comment,
	["texture"]				= String texture,
	["hash"]				= String hash,
	["comment_hash"]		= String comment_hash,
	["texture_hash"]		= String texture_hash,
}
```
___

### `OnUserStartSpeaking (mumble.client client, mumble.user)`

Called when a user starts to transmit voice data.
___

### `OnUserStopSpeaking (mumble.client client, mumble.user)`

Called when a user stops transmitting voice data.
___

### `OnUserSpeak (mumble.client client, Table event)`

Called when a user starts to transmit voice data.

``` lua
Table event = {
	["user"]			= mumble.user user,
	["codec"]			= Number codec,
	["target"]			= Number target,
	["sequence"]		= Number sequence,
	["data"]			= String encoded_opus_packet,
	["frame_header"]	= Number frame_header, -- The frame header usually contains a length and terminator bit
	["speaking"]		= Boolean speaking,
}
```
___

### `OnBanList (mumble.client client, Table banlist)`

Called on response to a `mumble.client:requestBanList()` call

```lua
Table banlist = {
	[1] = {
		["address"] = {
			["string"]	= String ipaddress,
			["ipv4"]	= Boolean isipv4,
			["ipv6"]	= Boolean isipv6,
			["data"]	= Table raw,
		},
		["mask"]		= Number ip_mask,
		["name"]		= String name,
		["hash"]		= String hash,
		["reason"]		= String reason,
		["start"]		= String start,
		["duration"]	= Number duration
	},
	...
}
```
___

### `OnMessage (mumble.client client, Table event)`

Called when the bot receives a text message

``` lua
Table event = {
	["actor"]		= mumble.user actor,
	["message"]		= String message,
	["users"]		= Table users,
	["channels"]	= Table channels -- will be nil when receiving a direct message
}
```
___

### `OnPermissionDenied (mumble.client client, Table event)`

Called when an action is performed that you don't have permission to do

``` lua
Table event = {
	["type"]		= Number type,
	["permission"]	= Number permission,
	["channel"]		= mumble.channel channel,
	["user"]		= mumble.user user,
	["reason"]		= String reason,
	["name"]		= String name,
}
```
___

### `OnACL (mumble.client client, Table acl)`

Called when ACL data is received from a `mumble.channel:requestACL()` request

```lua
Table acl = {
	["channel"]      = mumble.channel channel,
	["inherit_acls"] = Boolean inherit_acls,
	["groups"]       = {
		[1] = {
			["name"]        = String group_name,
			["inherited"]   = Boolean inherited,
			["inheritable"] = Boolean inheritable,
			["add"] = {
				[1] = Number user_id,
				...
			},
			["remove"] = {
				[1] = Number user_id,
				...
			},
			["inherited_members"] = {
				[1] = Number user_id,
				...
			}
		},
		...
	},
	["acls"]       = {
		[1] = {
			["apply_here"]  = Boolean apply_here,
			["apply_subs"]  = Boolean apply_subs,
			["inherited"]   = Boolean inherited,
			["user_id"]     = Number user_id,
			["group"]       = String group,
			["grant"]       = Number grant, -- This number is a flag that determines what this group is allowed to do
			["deny"]        = Number deny,  -- This number is a flag that determines what this group is NOT allowed to do
		},
		...
	},
}
```
___

### `OnCryptSetup (mumble.client client, Table event)`

Called when the server sends UDP encryption keys to the client.

``` lua
Table event = {
	["valid"]			= Boolean valid,
	["key"]				= String key,
	["client_nonce"]	= String client_nonce,
	["server_nonce"]	= String server_nonce,
}
```
___

### `OnUserList (mumble.client client, Table userlist)`

Called on response to a `mumble.client:requestUsers()` call

```lua
Table userlist = {
	[1] = {
		["user_id"]			= Number user_id,
		["name"]			= String name,
		["last_seen"]		= String last_seen,
		["last_channel"]	= mumble.channel last_channel,
	},
	...
}
```
___

### `OnPermissionQuery (mumble.client client, Table event)`

Called when the bot recieves permissions for a channel.

``` lua
Table event = {
	["channel"]			= mumble.channel channel,
	["permissions"]		= Number permissions,
	["flush"]			= Boolean flush,
}
```
___

### `OnCodecVersion (mumble.client client, Table event)`

Called when the bot recieves the codec info from the server.

``` lua
Table event = {
	["alpha"]			= Number alpha,
	["beta"]			= Number beta,
	["prefer_alpha"]	= Boolean prefer_alpha,
	["opus"]			= Boolean opus,
}
```
___

### `OnUserStats (mumble.client client, Table event)`

Called when the mumble.user's detailed statistics are received from the server.
Only sent if mumble.user:requestStats() is called.

``` lua
Table event = {
	["user"]				= mumble.user actor,
	["stats_only"]			= Boolean stats_only,
	["certificates"]		= Table certificates,
	["from_client"]			= {
		["good"]	= Number good,
		["late"]	= Number late,
		["lost"]	= Number lost,
		["resync"]	= Number resync,
	},
	["from_server"]			= {
		["good"]	= Number good,
		["late"]	= Number late,
		["lost"]	= Number lost,
		["resync"]	= Number resync,
	},
	["udp_packets"]			= Number udp_packets,
	["tcp_packets"]			= Number tcp_packets,
	["udp_ping_avg"]		= Number udp_ping_avg,
	["udp_ping_var"]		= Number udp_ping_var,
	["tcp_ping_avg"]		= Number tcp_ping_avg,
	["tcp_ping_var"]		= Number tcp_ping_var,
	["version"]				= Number version,
	["release"]				= String release,
	["os"]					= String os,
	["os_version"]			= String os_version,
	["certificates"]		= Table celt_versions,
	["address"]				= {
		["string"]	= String ipaddress,
		["ipv4"]	= Boolean isipv4,
		["ipv6"]	= Boolean isipv6,
		["data"]	= Table raw,
	},
	["bandwidth"]			= Number bandwidth,
	["onlinesecs"]			= Number onlinesecs,
	["idlesecs"]			= Number idlesecs,
	["strong_certificate"]	= Boolean strong_certificate,
	["opus"]				= Boolean opus,
}
```
___

### `OnServerConfig (mumble.client client, Table event)`

Called when the servers settings are received.
Usually called after OnServerSync

``` lua
Table event = {
	["max_bandwidth"]			= Number max_bandwidth,
	["welcome_text"]			= String welcome_text,
	["allow_html"]				= Boolean allow_html,
	["message_length"]			= Number message_length,
	["image_message_length"]	= Number image_message_length,
	["max_users"]				= Number max_users,
}
```
___

### `OnSuggestConfig (mumble.client client, Table event)`

Called when the servers suggest the client to use specific settings.

``` lua
Table event = {
	["version"]					= Number version,
	["positional"]				= Boolean positional,
	["push_to_talk"]			= Boolean push_to_talk,
}
```
___

### `OnPluginData (mumble.client client, Table event)`

Called when the client receives plugin data from the server.

``` lua
Table event = {
	["sender"] = mumble.user sender, -- Who sent this data packet
	["id"]     = Number id,          -- The data ID of this packet
	["data"]   = String data,        -- The data sent (can be binary data)
	["receivers"]				= {  -- A table of who is receiving this data
		[1] = mumble.user,
		...
	},
}
```
___

### `OnError (mumble.client client, String error)`

Called when an error occurs inside a hook.
WARNING: Erroring within this hook will cause an error on the line where mumble.loop() is called, causing the script to exit
___

### `OnPingTCP (mumble.client client, Table event)`

Called just before a TCP ping is sent to the server.
This updates the users statistics found on their information panel.
The mumble.client will automatically ping the server every 30 seconds within mumble.loop()

``` lua
Table event = {
	["timestamp"]		= Number timestamp,
	["good"]			= Number good,
	["late"]			= Number late,
	["lost"]			= Number lost,
	["resync"]			= Number resync,
	["udp_packets"]		= Number udp_packets,
	["tcp_packets"]		= Number tcp_packets,
	["udp_ping_avg"]	= Number udp_ping_avg,
	["udp_ping_var"]	= Number udp_ping_var,
	["tcp_ping_avg"]	= Number tcp_ping_avg,
	["tcp_ping_var"]	= Number tcp_ping_var,
}
```
___

### `OnPingUDP (mumble.client client, Table event)`

Called just before a UDP ping is sent to the server.
The mumble.client will automatically ping the server every 30 seconds within mumble.loop()

``` lua
Table event = {
	["timestamp"]		= Number timestamp,
}
```
___

### `OnAudioStreamEnd (mumble.client client, mumble.audiostream stream)`

Called when a sound file has finished playing.
Passes the number of the audio stream that finished.
