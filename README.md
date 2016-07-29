# lua-mumble
A lua module to connect to a mumble server and interact with it

## Usage

#### mumble

``` lua
-- Create a new mumble.client with a certificate and key file
-- Returns nil and an error string if something went wrong
mumble.client, [ String error ] = mumble.new(string Certificate File Path, string Key File Path)

-- The client's user
mumble.user = mumble.me
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

-- Adds a callback for a specific event
-- If no unique name is passed, it will default to "hook"
mumble.client:hook(String hook, [ String unique name = "hook"], Function callback)

-- Gets all registered callbacks
Table hooks = mumble.client:getHooks()

-- Returns a table of all mumble.users
Table users = mumble.client:getUsers()

-- Returns a table of all mumble.channels
Table channels = mumble.client:getChannels()

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
mumble.user:mute([ Boolean mute = true ])

-- Attempts to deafen a user
-- If no boolean is passed, it will default to deafening the user
mumble.user:deafen([ Boolean deaf = true ])

-- Attempts to change the users commend
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