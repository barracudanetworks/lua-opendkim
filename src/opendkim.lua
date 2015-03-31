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

local DKIM_STAT_CBTRYAGAIN = core.DKIM_STAT_CBTRYAGAIN

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

local sig_process; sig_process = core.interpose("DKIM_SIGINFO*", "sig_process", function (self)
	local dkim = self:getowner()

	while true do
		local ok, msg, stat = sig_process(self)

		if ok then
			break
		elseif stat ~= DKIM_STAT_CBTRYAGAIN then
			return ok, msg, stat
		else
			dkim:dopending()
		end
	end

	return true
end) -- :sig_process

local sig_process; sig_process = core.interpose("DKIM*", "sig_process", function (self, siginfo)
	while true do
		local ok, msg, stat = sig_process(self, siginfo)

		if ok then
			break
		elseif stat ~= DKIM_STAT_CBTRYAGAIN then
			return ok, msg, stat
		else
			self:dopending()
		end
	end

	return true
end) -- :sig_process

local eoh; eoh = core.interpose("DKIM*", "eoh", function (self)
	while true do
		local ok, msg, stat = eoh(self)

		if ok then
			break
		elseif stat ~= DKIM_STAT_CBTRYAGAIN then
			return ok, msg, stat
		else
			self:dopending()
		end
	end

	return true
end) -- :eoh

local eom; eom = core.interpose("DKIM*", "eom", function (self)
	while true do
		local ok, msg, stat = eom(self)

		if ok then
			break
		elseif stat ~= DKIM_STAT_CBTRYAGAIN then
			return ok, msg, stat
		else
			self:dopending()
		end
	end

	return true
end) -- :eom

return core
