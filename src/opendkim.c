#include <string.h> /* strerror_r(3) */
#include <errno.h>  /* ENOMEM errno */

#include <opendkim/dkim.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#if defined OPENDKIM_LUA_VERSION_NUM && OPENDKIM_LUA_VERSION_NUM != LUA_VERSION_NUM
#error Lua version mismatch
#endif


/*
 * F E A T U R E / E N V I R O N M E N T  M A C R O S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#if defined __GLIBC_PREREQ
#define GLIBC_PREREQ(M, m) (defined __GLIBC__ && __GLIBC_PREREQ(M, m) && !__UCLIBC__)
#else
#define GLIBC_PREREQ(M, m) 0
#endif

#define UCLIBC_PREREQ(M, m, p) (defined __UCLIBC__ && (__UCLIBC_MAJOR__ > M || (__UCLIBC_MAJOR__ == M && __UCLIBC_MINOR__ > m) || (__UCLIBC_MAJOR__ == M && __UCLIBC_MINOR__ == m && __UCLIBC_SUBLEVEL__ >= p)))

#ifndef STRERROR_R_CHAR_P
#define STRERROR_R_CHAR_P ((GLIBC_PREREQ(0,0) || UCLIBC_PREREQ(0,0,0)) && (_GNU_SOURCE || !(_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600)))
#endif


/*
 * L U A  C O M P A T A B I L I T Y  A P I
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#if LUA_VERSION_NUM < 502

static int lua_absindex(lua_State *L, int index) {
	return (index > 0 || index <= LUA_REGISTRYINDEX)? index : lua_gettop(L) + index + 1;
} /* lua_absindex() */

static void luaL_testudata(lua_State *L, int index, const char *tname) {
	void *p = lua_touserdata(L, index);
	int eq; 

	if (!p || !lua_getmetatable(L, index))
		return 0;

	luaL_getmetatable(L, tname);
	eq = lua_rawequal(L, -2, -1);
	lua_pop(L, 2);

	return (eq)? p : 0;
} /* luaL_testudata() */

static void luaL_setmetatable(lua_State *L, const char *tname) {
	luaL_getmetatable(L, tname);
	lua_setmetatable(L, -2);
} /* luaL_setmetatable() */

static void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup) {
	int i, t = lua_absindex(L, -1 - nup);

	for (; l->name; l++) {
		for (i = 0; i < nup; i++)
			lua_pushvalue(L, -nup);
		lua_pushcclosure(L, l->func, nup);
		lua_setfield(L, t, l->name);
	}

	lua_pop(L, nup);
} /* luaL_setfuncs() */

#define luaL_newlibtable(L, l) \
	lua_createtable(L, 0, (sizeof (l) / sizeof *(l)) - 1)

#define luaL_newlib(L, l) \
	(luaL_newlibtable((L), (l)), luaL_setfuncs((L), (l), 0))

#define lua_rawlen lua_objlen

#endif /* LUA_VERSION_NUM < 502 */


/*
 * A U X I L I A R Y  C  A N D  L U A  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

typedef int auxref_t;

static auxref_t auxL_ref(lua_State *L, int index) {
	lua_pushvalue(L, index);
	return luaL_ref(L, LUA_REGISTRYINDEX);
} /* auxL_ref() */

static void auxL_unref(lua_State *L, auxref_t *ref) {
	luaL_unref(L, LUA_REGISTRYINDEX, *ref);
	*ref = LUA_NOREF;
} /* auxL_unref() */

static int auxL_newmetatable(lua_State *L, const char *tname, const luaL_Reg *methods, const luaL_Reg *metamethods, int nup) {
	int i;

	if (!luaL_newmetatable(L, tname))
		return 0;

	/* copy upvalues to top of stack */
	for (i = 0; i < nup; i++) {
		lua_pushvalue(L, -1 - nup);
	}

	/* add metamethods to metatable */
	luaL_setfuncs(L, metamethods, nup);

	/* create __index table */
	lua_newtable(L);

	/* copy upvalues to top of stack */
	for (i = 0; i < nup; i++) {
		lua_pushvalue(L, -2 - nup);
	}

	/* add methods to __index table */
	luaL_setfuncs(L, methods, nup);

	/* add __index table to metatable */
	lua_setfield(L, -2, "__index");

	/* pop all upvalues, leaving metatable at top of stack */
	if (nup > 0) {
		lua_replace(L, -nup); /* replace first upvalue */
		lua_pop(L, nup - 1); /* pop remaining upvalues */
	}

	return 1;
} /* auxL_newmetatable() */

static int aux_strerror_r(int error, char *dst, size_t lim) {
#if STRERROR_R_CHAR_P
	char *src;

	if (!(src = strerror_r(error, dst, lim)))
		return EINVAL;

	if (src != dst && lim > 0) {
		size_t n = strnlen(src, lim - 1);
		memcpy(dst, src, n);
		dst[n] = '\0';
	}

	return 0;
#else
	/* glibc between 2.3.4 and 2.13 returns -1 on error */
	if (-1 == (error = strerror_r(error, dst, lim))) {
		return errno;
	} else {
		return error;
	}
#endif
} /* aux_strerror_r() */

static const char *auxL_strerror(lua_State *L, int error) {
	luaL_Buffer B;
	char *buf;
	size_t bufsiz;
	int top error;

	top = lua_gettop(L);

	luaL_buffinit(L, &B);
	buf = luaL_prepbuffer(&B);
	bufsiz = LUAL_BUFFERSIZE;

	if (0 == aux_strerror_r(error, buf, bufsiz)) {
		luaL_addsize(&B, strlen(buf));
		luaL_pushresult(&B);

		return lua_tostring(L, -1);
	}

	lua_settop(L, top); /* discard buffer temporaries */

	return lua_pushfstring(L, "Unknown error: %d", error);
} /* auxL_strerror() */

static int auxL_pusherror(lua_State *L, int error, const char *how) {
	int top = lua_gettop(L);

	for (; *how; how++) {
		switch (*how) {
		case '~':
			lua_pushnil(L);
			break;
		case '0':
			lua_pushboolean(L, 0);
			break;
		case '#':
			lua_pushinteger(L, error);
			break;
		case '$':
			auxL_strerror(L, error);
			break;
		default:
			lua_pushnil(L);
			break;
		}
	}

	return lua_gettop(L) - top;
} /* auxL_pusherror() */

static int auxL_pushstat(lua_State *L, DKIM_STAT error, const char *how) {
	int top = lua_gettop(L);

	for (; *how; how++) {
		switch (*how) {
		case '~':
			lua_pushnil(L);
			break;
		case '0':
			lua_pushboolean(L, 0);
			break;
		case '#':
			lua_pushinteger(L, error);
			break;
		case '$':
			lua_pushstring(L, dkim_getresultstr(error));
			break;
		default:
			lua_pushnil(L);
			break;
		}
	}

	return lua_gettop(L) - top;
} /* auxL_pushstat() */


/*
 * (DKIM *) B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

typedef struct {
	DKIM *ctx;
	auxref_t lib; /* reference key to DKIM_LIB user data */
	auxref_t key_lookup; /* reference key to Lua callback function */
} DKIM_State;

static const DKIM_State DKIM_initializer = {
	.dkim = NULL,
	.key_lookup = LUA_NOREF,
};

static DKIM_State *DKIM_checkself(lua_State *L, int index) {
	DKIM_State *dkim = luaL_checkudata(L, index, "DKIM*");

	luaL_argcheck(L, dkim->ctx, index, "attempt to use a closed DKIM handle");

	return dkim;
} /* DKIM_LIB_checkself() */

static DKIM_State *DKIM_prep(lua_State *L, int index) {
	DKIM_State *dkim;

	index = lua_absindex(L, index);

	dkim = lua_newuserdata(L, sizeof *dkim);
	*dkim = DKIM_initializer;
	luaL_setmetatable(L, "DKIM*");
	dkim->lib = auxL_ref(L, index);

	return dkim;
} /* DKIM_prep() */

static int DKIM__gc(lua_State *L) {
	DKIM_State *dkim = luaL_checkudata(L, 1, "DKIM*");

	if (dkim->ctx) {
		dkim_free(dkim->ctx);
		dkim->ctx = NULL;
	}

	auxL_unref(L, &dkim->lib);

	return 0;
} /* DKIM__gc() */

static luaL_Reg DKIM_methods[] = {
	{ NULL, NULL },
}; /* DKIM_methods[] */

static luaL_Reg DKIM_metamethods[] = {
	{ "__gc", &DKIM__gc },
	{ NULL,   NULL },
}; /* DKIM_metamethods[] */


/*
 * (DKIM_LIB *) B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

typedef struct {
	DKIM_LIB *ctx;
} DKIM_LIB_State;

static DKIM_LIB_State *DKIM_LIB_checkself(lua_State *L, int index) {
	DKIM_LIB_State *lib = luaL_checkudata(L, index, "DKIM_LIB*");

	luaL_argcheck(L, lib->ctx, index, "attempt to use a closed DKIM_LIB handle");

	return lib;
} /* DKIM_LIB_checkself() */

static DKIM_LIB_State *DKIM_LIB_prep(lua_State *L) {
	DKIM_LIB_State *lib;

	lib = lua_newuserdata(L, sizeof *lib);
	memset(lib, 0, sizeof *lib);
	luaL_setmetatable(L, "DKIM*");

	return lib;
} /* DKIM_LIB_prep() */

static int DKIM_LIB_sign(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);

	return 0;
} /* DKIM_LIB_sign() */

static int DKIM_LIB_verify(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);

	return 0;
} /* DKIM_LIB_verify() */

static int DKIM_LIB__gc(lua_State *L) {
	DKIM_LIB_State *lib = luaL_checkudata(L, 1, "DKIM_LIB*");

	if (lib->ctx) {
		dkim_close(lib->ctx);
		lib->ctx = NULL;
	}

	return 0;
} /* DKIM_LIB__gc() */

static luaL_Reg DKIM_LIB_methods[] = {
	{ "sign",   lib_sign },
	{ "verify", lib_verify },
	{ NULL,     NULL },
}; /* DKIM_LIB_methods[] */

static luaL_Reg DKIM_LIB_metamethods[] = {
	{ "__gc",   lib__gc },
	{ NULL,     NULL },
}; /* DKIM_LIB_metamethods[] */


/*
 * O P E N D K I M  T O P - L E V E L  B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static int opendkim_init(lua_State *L) {
	DKIM_LIB_State *lib;
	int error;

	lua_settop(L, 1);

	lib = DKIM_LIB_prep(L);

	if (!(lib->ctx = dkim_init(NULL, NULL))) {
		error = ENOMEM;
		goto error;
	}

	return 1;
error:
	lua_pushnil(L);
	auxL_strerror(L, error);
	lua_pushinteger(L, error);

	return 3;
} /* opendkim_init() */

static luaL_Reg opendkim_globals[] = {
	{ "init", opendkim_init },
	{ NULL,   NULL },
}; /* opendkim_globals[] */

int luaopen_opendkim(lua_State *L) {
	auxL_newmetatable(L, "DKIM_LIB*", DKIM_LIB_methods, DKIM_LIB_metamethods, 0);
	lua_pop(L, 1);

	auxL_newmetatable(L, "DKIM*", DKIM_methods, DKIM_metamethods, 0);
	lua_pop(L, 1);

	luaL_newlibtable(L, opendkim_globals);

	return 1;
} /* luaopen_opendkim() */
