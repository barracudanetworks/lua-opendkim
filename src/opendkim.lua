-- ==========================================================================
-- opendkim.lua - Lua bindings to OpenDKIM's libopendkim API.
-- --------------------------------------------------------------------------
-- Copyright (c) 2015 Barracuda Networks, Inc.
--
-- Permission is hereby granted, free of charge, to any person obtaining a
-- copy of this software and associated documentation files (the
-- "Software"), to deal in the Software without restriction, including
-- without limitation the rights to use, copy, modify, merge, publish,
-- distribute, sublicense, and/or sell copies of the Software, and to permit
-- persons to whom the Software is furnished to do so, subject to the
-- following conditions:
--
-- The above copyright notice and this permission notice shall be included
-- in all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
-- OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
-- MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
-- NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
-- DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
-- OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
-- USE OR OTHER DEALINGS IN THE SOFTWARE.
-- ==========================================================================
local core = require"opendkim.core"

--
-- We have to issue callbacks from Lua script because the Lua 5.1 API
-- doesn't support lua_callk. A callback might need to yield.
--
-- :getpending returns a list of callback information:
--
-- 	* postf - Function to pass the callback return values. These are
-- 	          saved and utilized by the C code hook that libopendkim
-- 	          actually calls. The C hook returns DKIM_CBSTAT_TRYAGAIN
-- 	          until we execute postf to set the real return values.
-- 	* execf - The application-defined callback function, the results
-- 	          of which we will be passed through to postf.
-- 	* self  - DKIM instance, needed by both postf and execf.
-- 	* ...   - Variable list of parameters to pass to execf.
--
local function dopending(postf, execf, self, ...)
	if postf then
		return true, postf(self, execf(self, ...))
	else
		return false
	end
end -- dopending

core.interpose("DKIM*", "dopending", function (self)
	--
	-- Loop over :getpending until there are no more pending callbacks.
	--
	-- Because the number of arguments to pass the application-defined
	-- callback is variable, we pass the result of :getpending directly
	-- to our helper function, allowing us to use variable argument list
	-- notation. The helper function returns true if it issued a
	-- callback, or false if :getpending returned nothing.
	--
	local count = -1

	repeat
		local did = dopending(self:getpending())
		count = count + 1
	until not did

	return count
end) -- :dopending

local function iowrap(class, method)
	local DKIM_STAT_CBTRYAGAIN = core.DKIM_STAT_CBTRYAGAIN

	local f; f = core.interpose(class, method, function (self, ...)
		local dkim = class == "DKIM*" and self or self:getowner()

		while true do
			local ok, msg, stat = f(self, ...)

			if ok then
				break
			elseif stat ~= DKIM_STAT_CBTRYAGAIN then
				return ok, msg, stat
			else
				dkim:dopending()
			end
		end

		return true
	end)
end -- iowrap

iowrap("DKIM_SIGINFO*", "process")
iowrap("DKIM*", "sig_process")
iowrap("DKIM*", "eoh")
iowrap("DKIM*", "eom")
iowrap("DKIM*", "chunk")


--
-- lib:setflag, lib:unsetflag, lib:issetflag - Auxiliary routines to
-- simplify management of option flags.
--
local DKIM_OP_GETOPT = core.DKIM_OP_GETOPT
local DKIM_OP_SETOPT = core.DKIM_OP_SETOPT
local DKIM_OPTS_FLAGS = core.DKIM_OPTS_FLAGS

local band = core.band
local bnot = core.bnot
local bor = core.bor
local btest = core.btest

local libflags = 0

for k, flag in pairs(core) do
	if k:match("^DKIM_LIBFLAGS_") then
		libflags = bor(libflags, flag)
	end
end

local function checkflag(flag)
	local extra

	if type(flag) ~= "number" then
		return error(string.format("expected integer flag, got %s)", type(flag)), 2)
	elseif not btest(libflags, flag) or 0 ~= (band(flag, flag - 1)) then
		return error(string.format("expected flag, got %d (not in set of DKIM_LIBFLAGS)", flag), 2)
	end

	return flag
end -- checkflag

core.interpose("DKIM_LIB*", "setflag", function (self, flag)
	local flags = self:options(DKIM_OP_GETOPT, DKIM_OPTS_FLAGS)

	return self:options(DKIM_OP_SETOPT, DKIM_OPTS_FLAGS, bor(flags, checkflag(flag)))
end)

core.interpose("DKIM_LIB*", "unsetflag", function (self, flag)
	local flags = self:options(DKIM_OP_GETOPT, DKIM_OPTS_FLAGS)

	return self:options(DKIM_OP_SETOPT, DKIM_OPTS_FLAGS, band(flags, bnot(checkflag(flag))))
end)

core.interpose("DKIM_LIB*", "issetflag", function (self, flag)
	local flags = self:options(DKIM_OP_GETOPT, DKIM_OPTS_FLAGS)

	return btest(flags, checkflag(flag))
end)

--
-- core.strconst - Auxiliary routine to convert constant to string.
--
local cache = {}

function core.strconst(match, c)
	local cached = cache[match] and cache[match][c]

	if cached then
		return cached
	end

	for k, v in pairs(core) do
		if v == c then
			local name = k:match(match)

			if name then
				if not cache[match] then
					cache[match] = {}
				end

				cache[match][c] = name

				return name
			end
		end
	end
end -- core.strconst

return core
