# lua-mumble
A lua module to connect to a mumble server and interact with it

## Usage

#### mumble

``` lua
-- Create a new mumble.client with a certificate and key file
-- Returns nil and an error string if something went wrong
mumble.client, [ String error ] = mumble.new(String certificate file path, String key file path)

-- Create a new opus encoder
-- Returns nil and an error string if something went wrong
mumble.opus, [ String error ] = mumble.encoder([ Number samplerate = 48000 ])

-- The client's user
mumble.user = mumble.client.me
```

#### mumble.client

``` lua
-- Connect to a mumble server
-- Returns the current mumble.client userdata if successful, otherwise returns false and an error string
mumble.client client, [ String error ] = mumble.client:connect(String host, [ Number port = 64738 ])

-- Disconnect from a mumble server
mumble.client:disconnect()

-- Called to process the internal socket and callbacks
mumble.client:update()

-- Play an ogg audio file
mumble.client:play(mumble.opus encoder, String ogg file path)

-- Checks if the client is currently playing an audio file
Boolean playing = mumble.client:isPlaying()

-- Stops playing the current audio
mumble.client:stopPlaying()

-- Sets the global volume level
-- Consider this the master volume level
mumble.client:setVolume(Number volume)

-- Gets the global volume level
Number volume = mumble.client:getVolume()

-- Adds a callback for a specific event
-- If no unique name is passed, it will default to "hook"
mumble.client:hook(String hook, [ String unique name = "hook"], Function callback)

-- Gets all registered callbacks
Table hooks = mumble.client:getHooks()

-- Structure
Table hooks = {
	["onServerSync"] = {
		["hook"] = function: 0xffffffff,
		["do stuff on connection"] = function: 0xffffffff,
	},
	["onServerPing"] = {
		["hook"] = function: 0xffffffff,
		["do stuff on ping"] = function: 0xffffffff,
	}
}

-- Returns a table of all mumble.users
Table users = mumble.client:getUsers()

-- Structure
-- Key:		session number
-- Value:	mumble.user
Table users = {
	[session] = mumble.user,
	[session] = mumble.user,
	[session] = mumble.user,
	[session] = mumble.user,
	[session] = mumble.user,
}

-- Returns a table of all mumble.channels
Table channels = mumble.client:getChannels()

-- Structure
-- Key:		channel id
-- Value:	mumble.channel
Table channels = {
	[channel_id] = mumble.channel,
	[channel_id] = mumble.channel,
	[channel_id] = mumble.channel,
	[channel_id] = mumble.channel,
	[channel_id] = mumble.channel,
}

-- A timestamp in milliseconds
Number time = mumble.client:gettime()
```

#### mumble.user

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

-- Attempts to change the users comment
mumble.user:comment(String comment)

-- Attempts to register the users name to the server
mumble.user:register()

-- Requests the users information statistics from the server
-- If no boolean is passed, it will default to requesting ALL statistics
mumble.user:requestStats([ Boolean statsonly = false ])
```

#### mumble.channel

``` lua
-- Sends a text message to the entire channel
mumble.channel:message(String message)

-- Attempts to set the channels description
mumble.channel:setDescription(String description)

-- Attempts to remove the channel
mumble.channel:remove()
```

#### mumble.audio

``` lua
-- Stops playing the audio
mumble.audio:stop()

-- Sets the volume level
mumble.audio:setVolume(Number volume)

-- Gets the volume level
Number volume = mumble.audio:getVolume()
```

#### mumble.opus

``` lua
-- Sends a text message to a user
mumble.opus:setBitRate(Number bitrate)
```

### hooks

 - `onServerVersion`

``` lua
Table event = {
	["version"]		= Number version,
	["release"]		= String release,
	["os"]			= String os,
	["os_version"]	= String os_version,
}
```

 - `onServerPing`

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

 - `onServerReject`

``` lua
Table event = {
	["type"]	= mumble.reject type,
	["reason"]	= String reason,
}
```

 - `onServerSync`

``` lua
Table event = {
	["user"]			= mumble.user user,
	["max_bandwidth"]	= Number max_bandwidth,
	["welcome_text"]	= String welcome_text,
	["permissions"]		= Number permissions,
}
```

 - `onChannelRemove`

``` lua
Table event = {
	["channel"]	= mumble.channel channel,
}
```

 - `onChannelState`

``` lua
Table event = {
	["channel"]				= mumble.channel channel,
	["parent"]				= mumble.channel parent,
	["name"]				= String name,
	["description"]			= String description,
	["temporary"]			= Boolean temporary,
	["description_hash"]	= String description_hash,
}
```

 - `onUserRemove`

``` lua
Table event = {
	["user"]	= mumble.user user,
	["actor"]	= mumble.user actor,
	["reason"]	= String reason,
	["ban"]		= Boolean ban,
}
```

 - `onUserState`

``` lua
Table event = {
	["actor"]	= mumble.user actor,
	["user"]	= mumble.user user,
}
```

 - `onMessage`

``` lua
Table event = {
	["actor"]		= mumble.user actor,
	["message"]		= String message,
	["users"]		= Table users,
	["channels"]	= Table channels
}
```

 - `onPermissionDenied`

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

 - `onCodecVersion`

``` lua
Table event = {
	["alpha"]			= Number alpha,
	["beta"]			= Number beta,
	["prefer_alpha"]	= Boolean prefer_alpha,
	["opus"]			= Boolean opus,
}
```

 - `onUserStats`

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
	["address"]				= String address,
	["bandwidth"]			= Number bandwidth,
	["onlinesecs"]			= Number onlinesecs,
	["idlesecs"]			= Number idlesecs,
	["strong_certificate"]	= Boolean strong_certificate,
	["opus"]				= Boolean opus,
}
```

 - `onServerConfig`

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