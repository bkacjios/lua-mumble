# lua-mumble

A lua module to connect to a mumble server and interact with it

## Build requirements

```
sudo apt-get install libluajit-5.1-dev protobuf-c libprotobuf-c-dev libssl-dev libopus-dev libvorbis-dev 
```

## Usage

### mumble

``` lua
-- Connect to a mumble server
-- Returns nil and an error string if something went wrong
mumble.client, [ String error ] = mumble.connect(String host, Number port, String certificate file path, String key file path)

-- The client's user
mumble.user = mumble.client.me

-- A new voicetarget object
mumble.voicetarget = mumble.voicetarget()

-- A timestamp in milliseconds
Number time = mumble.gettime()
```

### mumble.client

``` lua
-- Authenticate as a user
mumble.client:auth(String username)

-- Check if the client is connected
Boolean connected = mumble.client:isConnected()

-- Check if the client has fully synced all users and channels
Boolean synced = mumble.client:isSynced()

-- Disconnect from a mumble server
mumble.client:disconnect()

-- Called to process the internal socket and callbacks
mumble.client:update()

-- Play an ogg audio file
mumble.client:play(String ogg file path)

-- Checks if the client is currently playing an audio file
Boolean playing = mumble.client:isPlaying()

-- Stops playing the current audio
mumble.client:stopPlaying()

-- Sets the global volume level
-- Consider this the master volume level
mumble.client:setVolume(Number volume)

-- Gets the global volume level
Number volume = mumble.client:getVolume()

-- Attempts to change the bots comment
mumble.client:setComment(String comment)

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

-- Register a mumble.voicetarget to the server
-- Accepts multiple mumble.voicetarget objects that will all be assigned to the given ID
mumble.client:registerVoiceTarget(Number id, mumble.voicetarget ...)

-- Set the current voice target that mumble.client:play() will abide by
-- Defaults to 0, the default voice target
mumble.client:setVoiceTarget(Number id)

-- Get the current voice target
Number id = mumble.client:getVoiceTarget()

-- Get the uptime of the current client
Number time = mumble.client:getUpTime()

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

mumble.channel channel = mumble.client:getChannel(String path)

-- Structure
-- Key:		channel id
-- Value:	mumble.channel
Table channels = {
	[id] = mumble.channel,
	[id] = mumble.channel,
	[id] = mumble.channel,
	[id] = mumble.channel,
	[id] = mumble.channel,
}
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
Number userid = mumble.user:getID()

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
Number id = mumble.channel:getID()

-- Gets the parent channel
-- Returns nil on root channel
mumble.channel channel = mumble.channel:getParent()

-- Returns the channels that are parented to the channel
Table children = mumble.channel:getChildren()

-- Returns the users that are currently within the channel
Table users = mumble.channel:getUsers()

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
mumble.channel:link(mumble.channel ...)
```

### mumble.voicetarget

``` lua
-- Add a user to whisper to
mumble.voicetarget:addUser(mumble.user user)

-- Sets the channel that is be shouted to
mumble.voicetarget:setChannel(mumble.channel channel)

-- Gets the channel that is shouted to
mumble.voicetarget:getChannel()

-- Sets the specific user group to whisper to
mumble.voicetarget:setGroup(String group)

-- Shout to the linked channels of the set channel
mumble.voicetarget:setLinks(Boolean followlinks)

-- Shout to the children of the set channel
mumble.voicetarget:setChildren(Boolean followchildren)
```

## hooks

### `OnServerVersion (Table event)`

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

### `OnServerPing (Table event)`

Called when the server sends a responce to a ping request.
The mumble.client will automatically ping the server every 15 seconds within mumble.client:update()

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

### `OnServerReject (Table event)`

Called when you are rejected from connecting to the server.

``` lua
Table event = {
	["type"]	= mumble.reject type,
	["reason"]	= String reason,
}
```
___

### `OnServerSync (Table event)`

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

### `OnChannelRemove (mumble.channel channel)`

Called when a mumble.channel is removed.
___

### `OnChannelState (mumble.channel channel)`

Called when a mumble.channel state has changed.. Like updating the name, description, position, comment, etc..
___

### `OnUserChannel (Table event)`

Called when a mumble.user changes their channel

``` lua
Table event = {
	["user"]	= mumble.user user,
	["actor"]	= mumble.user actor,
	["from"]	= mumble.channel from,
	["to"]		= mumble.channel to,
}
```

### `OnUserRemove (Table event)`

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

### `OnUserConnected (Table event)`

Called when a mumble.user has connected to the server

``` lua
Table event = {
	["user"]	= mumble.user user,
}
```
___

### `OnUserState (Table event)`

Called when a mumble.user state has changed.. Like updating their comment, moving channels, muted, deafened, etc..

``` lua
Table event = {
	["actor"]	= mumble.user actor,
	["user"]	= mumble.user user,
}
```
___

### `OnMessage (Table event)`

Called when the bot receives a text message

``` lua
Table event = {
	["actor"]		= mumble.user actor,
	["message"]		= String message,
	["users"]		= Table users,
	["channels"]	= Table channels
}
```
___

### `OnPermissionDenied (Table event)`

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

### `OnCodecVersion (Table event)`

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

### `OnUserStats (Table event)`

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

### `OnServerConfig (Table event)`

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

### `OnError (String error)`

Called when a hook is erroring.
WARNING: Erroring within this hook will cause the script to exit!
___

### `OnTick ()`

Called whenever mumble.client:update() is called.
___

### `OnClientPing ()`

Called just before a ping is sent to the server.
___

### `OnAudioFinished ()`

Called when a sound file has finished playing.
___