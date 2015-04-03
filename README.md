# lua-opendkim

lua-opendkim is a Lua bindings module for OpenDKIM's libopendkim API. Aside
from providing comprehensive bindings to the API, the module is also
implemented in a way to make it simple to use yieldable Lua closures as
callback hooks, without any tricks or dependencies on the runtime
environment. This permits using any DNS implementation and event loop you
choose.

## Build

### Requirements

* GNU Make  
* libopendkim library and headers
* Lua 5.1/LuaJIT, Lua 5.2, and/or Lua 5.3

### configure  

There is no autoconf ./configure step, yet. Run `make configure` to detect
presence and location of Lua APIs, and to link in the 5.1, 5.2, and 5.3
build targets. `make configure` will also save CPPFLAGS, CFLAGS, LDFLAGS,
SOFLAGS, LIBS, prefix, libdir, and various other variables to be used during
subsequent make invocations.

Lua header and module installation paths are automatically detected. But you
can override them using `LUA51_CPPFLAGS`, `LUA52_CPPFLAGS`, and
`LUA53_CPPFLAGS`, `lua51path`, `lua51cpath`, `lua52path`, `lua52cpath`,
`lua53path`, `lua53cpath`.

#### Linux Example

```
make configure \
	CFLAGS="-fPIC -O2 -g -Wall -Wextra -std=gnu99" \
	SOFLAGS="-shared" \
	LIBS="-lopendkim"
```

#### OS X Example

``` 
make configure \
	CFLAGS="-O2 -g -Wall -Wextra -std=gnu99" \      
	CPPFLAGS="-I/usr/local/opendkim/include" \
	LDFLAGS="-L/usr/local/opendkim/lib" \
	SOFLAGS="-bundle -undefined dynamic_lookup" \
	LIBS="-lopendkim"
```

### make all

Build all Lua modules. Which modules to build is determined by `make
configure`.

### make install

Install all Lua modules. Which modules to install is determined by `make
configure`.
