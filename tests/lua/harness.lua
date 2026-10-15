-- A tiny test harness, so the tests run on any Lua with no extra modules
local harness = {}

local tests = {}

function harness.test(name, fn)
	tests[#tests + 1] = { name = name, fn = fn }
end

local function fail(message, level)
	error(message, (level or 1) + 2)
end

function harness.eq(actual, expected, message)
	if actual ~= expected then
		fail(string.format("%sexpected %s, got %s", message and (message .. ": ") or "", tostring(expected), tostring(actual)))
	end
end

function harness.near(actual, expected, epsilon, message)
	if type(actual) ~= "number" or math.abs(actual - expected) > epsilon then
		fail(string.format("%sexpected %s (+/- %s), got %s", message and (message .. ": ") or "", tostring(expected), tostring(epsilon), tostring(actual)))
	end
end

function harness.truthy(value, message)
	if not value then
		fail(message or "expected a truthy value")
	end
end

-- Expect fn to raise an error containing the plain text pattern
function harness.errors(pattern, fn, ...)
	local ok, err = pcall(fn, ...)
	if ok then
		fail("expected an error containing \"" .. pattern .. "\"")
	elseif not tostring(err):find(pattern, 1, true) then
		fail("expected an error containing \"" .. pattern .. "\", got \"" .. tostring(err) .. "\"")
	end
end

-- A path for a temporary file that doesn't exist yet
function harness.tmpname(suffix)
	local name = os.tmpname()
	os.remove(name)
	return name .. (suffix or "")
end

function harness.run()
	local failed = 0
	for _, test in ipairs(tests) do
		local ok, err = xpcall(test.fn, debug.traceback)
		if ok then
			print("PASS " .. test.name)
		else
			failed = failed + 1
			print("FAIL " .. test.name .. "\n" .. tostring(err))
		end
	end
	print(string.format("%d tests, %d failed", #tests, failed))
	os.exit(failed == 0 and 0 or 1)
end

return harness
