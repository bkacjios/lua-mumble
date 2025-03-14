local mumble = require("mumble")

local server = mumble.crypt()
local client = mumble.crypt()

print(server)
print(client)

-- Generate some keys on the server
server:genKey()

-- "Send" keys to the client
assert(client:setKey(server:getRawKey(), server:getDecryptIV(), server:getEncryptIV()),
	"CryptState: Cipher resync failed: Invalid key/nonce from the server")

assert(server:isValid(), "server cryptstate invalid")
assert(client:isValid(), "client cryptstate invalid")

print("isValid", server, client)

local TEST_MESSAGE = "Hello world!"

print("TESTING CLEINT -> SERVER")

local encrypted = assert(client:encrypt(TEST_MESSAGE), "failed to encrypt data")

print(string.format("string: %q\nencrypted: %q", TEST_MESSAGE, string.format("%q", encrypted)))

local decrypted = assert(server:decrypt(encrypted), "failed to decrypt data")

print(string.format("decrypted: %q", decrypted))

assert(decrypted == TEST_MESSAGE, "failed to decrypt data encrypted by client")

print("TESTING SERVER -> CLIENT")

encrypted = assert(server:encrypt(TEST_MESSAGE), "failed to encrypt data")

print(string.format("string: %q\nencrypted: %q", TEST_MESSAGE, string.format("%q", encrypted)))

decrypted = assert(client:decrypt(encrypted), "failed to decrypt data")

print(string.format("decrypted: %q", decrypted))

assert(decrypted == TEST_MESSAGE, "failed to decrypt data encrypted by server")

print("SERVER")
print("\tGood:", server:getGood())
print("\tLate:", server:getLate())
print("\tLost:", server:getLost())

print("CLIENT")
print("\tGood:", client:getGood())
print("\tLate:", client:getLate())
print("\tLost:", client:getLost())

print("PASSED")