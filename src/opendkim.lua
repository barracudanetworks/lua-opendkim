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


return core
