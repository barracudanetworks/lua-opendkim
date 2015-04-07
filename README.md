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
* Headers for Lua 5.1, Lua 5.2, or Lua 5.3 API.

### configure  

There is no autoconf ./configure step, yet. Run `make configure` to detect
presence and location of Lua APIs, and to link in the 5.1, 5.2, and 5.3
build targets. `make configure` will also save CPPFLAGS, CFLAGS, LDFLAGS,
SOFLAGS, LIBS, prefix, libdir, and various other variables to be used during
subsequent make invocations.

Lua header and module installation paths are automatically detected in most
cases. But you can override them using `LUA51_CPPFLAGS`, `LUA52_CPPFLAGS`,
and `LUA53_CPPFLAGS`, `lua51path`, `lua51cpath`, `lua52path`, `lua52cpath`,
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

## API

The Lua API mirrors the C API closely, but using a more object-oriented
style. Functions which operate on objects are defined as methods on the Lua
userdata binding, and any prefixes elided.

### Error Handling

Except for argument typing constraints, errors are returned using the
idiomatic Lua tuple, _nil_ (or _false_), reason string, reason code. The
reason code is usually a value from the DKIM_STAT enumeration, but this can
vary depending on the libopendkim function being bound--e.g. could be system
errno value, DKIM_SIGERROR enumeration value, etc.


### Module Globals

##### opendkim.init()

Returns a DKIM_LIB object on success. Otherwise _nil_, reason string, reason
code.

#### opendkim.libversion()

Returns libopendkim version number.

#### opendkim.ssl_version()

Returns underlying SSL version number.

#### opendkim.getresultstr(code)

Returns reason string corresponding to DKIM_STAT reason code.

#### opendkim.sig_geterrorstr(code)

Returns reason string corresponding to DKIM_SIGERROR reason code.

### DKIM_LIB Methods

#### lib:flush_cache()

Returns integer number of records flushed, or _nil_ if caching was not
enabled.

#### lib:getcachestats([reset])

Returns integer queries, hits, expired, and keys counters on sucess.
Otherwise _nil_, reason string, reason code.

If _reset_ is _true_, then the queries, hits, and expired counters are reset
to 0.

#### lib:libfeature(feature)

Returns _true_ if _feature_ is enabled, _false_ otherwise. _feature_ should
be one of the DKIM\_FEATURE\_ constants.

#### lib:set_final(f)

Sets a closure to handle sorting and selection of signatures during
verify:eom processing. Returns the previous closure, if any.

_f_ will receives two arguments: DKIM verify object, and table of
DKIM_SIGINFO signature objects. It should return a DKIM_CBSTAT enumeration
value.

(TODO: Sorting of the signatures table is not supported. Operations on the
table will not update the order of signature verification. Presently the
only useful operation is to disable particular signatures using sig:ignore.)

#### lib:set_key_lookup(f)

Sets a closure to handle DNS queries. Returns the previous closure, if any.

_f_ will receive two arguments: DKIM verify object, and DKIM_SIGINFO
signature object. The application should loop over the sig:getqueries table.
The first DNS record succcessfully found should be returned as a string.
Otherwise return a DKIM_CBSTAT enumeration value.

#### lib:set_prescreen()

Same as lib:set_final, except is called during verify:eoh processing.

#### lib:sign(id, key, selector, domain, [hdrcanon][, bodycanon][, algo][, length])

Returns a new DKIM instance for message signing.

#### lib:verify(id)

Returns a new DKIM instance for message verification.

#### lib:options(op, opt[, value])

Set or retrieve option. _op_ should be either DKIM_OP_GETOPT or
DKIM_OP_SETOPT. _opt_ should be one of the DKIM_OPTS\_ constants.

If op is DKIM_OP_SETOPT, _value_ should be a value of the appropriate type.
If the option is changed, returns _true_. Otherwise returns _false_, reason
string, reason code.

If op is DKIM_OP_GETOPT, the corresponding value is returned.
