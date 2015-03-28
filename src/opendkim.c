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

static void *luaL_testudata(lua_State *L, int index, const char *tname) {
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

static void auxL_unref(lua_State *L, auxref_t *ref) {
	luaL_unref(L, LUA_REGISTRYINDEX, *ref);
	*ref = LUA_NOREF;
} /* auxL_unref() */

static void auxL_ref(lua_State *L, int index, auxref_t *ref) {
	auxL_unref(L, ref);
	lua_pushvalue(L, index);
	*ref = luaL_ref(L, LUA_REGISTRYINDEX);
} /* auxL_ref() */

static void auxL_getref(lua_State *L, auxref_t ref) {
	if (ref == LUA_NOREF || ref == LUA_REFNIL) {
		lua_pushnil(L);
	} else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	}
} /* auxL_getref() */

static _Bool auxL_optboolean(lua_State *L, int index, _Bool def) {
	if (lua_isnoneornil(L, index))
		return def;

	return lua_toboolean(L, index);
} /* auxL_optboolean() */

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
	int top;

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
 * (DKIM_LIB_State *) and (DKIM_State *) D E F I N I T I O N S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

typedef struct {
	DKIM_LIB *ctx;
	auxref_t final; /* reference key to Lua callback function */
	auxref_t key_lookup; /* "" */
	auxref_t prescreen; /* "" */
} DKIM_LIB_State;

static const DKIM_LIB_State DKIM_LIB_initializer = {
	.ctx = NULL,
	.final = LUA_NOREF,
	.key_lookup = LUA_NOREF,
	.prescreen = LUA_NOREF,
};

#define DKIM_CB_FINAL      0x01
#define DKIM_CB_KEY_LOOKUP 0x02
#define DKIM_CB_PRESCREEN  0x04

typedef struct {
	DKIM *ctx;
	DKIM_LIB_State *lib;

	struct {
		auxref_t lib; /* DKIM_LIB_State anchor */
		auxref_t txt; /* key_lookup txt string anchor */
	} ref;

	struct {
		int exec;
		int done;

		struct {
			DKIM_SIGINFO **siglist;
			int sigcount;

			DKIM_STAT stat;
		} final;

		struct {
			DKIM_SIGINFO *siginfo;

			const char *txt;
			DKIM_STAT stat;
		} key_lookup;

		struct {
			DKIM_SIGINFO **siglist;
			int sigcount;

			DKIM_STAT stat;
		} prescreen;
	} cb;
} DKIM_State;

static const DKIM_State DKIM_initializer = {
	.ref = { .lib = LUA_NOREF, .txt = LUA_NOREF },
	.cb = {
		.key_lookup = { .stat = DKIM_CBSTAT_ERROR },
		.prescreen = { .stat = DKIM_CBSTAT_ERROR },
	},
};

typedef struct {
	DKIM_SIGINFO *ctx;
	auxref_t dkim;
} DKIM_SIGINFO_State;

static const DKIM_SIGINFO_State DKIM_SIGINFO_initializer = {
	.dkim = LUA_NOREF,
}; /* DKIM_SIGINFO_initializer */


/*
 * (DKIM_SIGINFO *) B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static DKIM_SIGINFO_State *DKIM_SIGINFO_checkself(lua_State *L, int index) {
	DKIM_SIGINFO_State *siginfo = luaL_checkudata(L, index, "DKIM_SIGINFO*");

	luaL_argcheck(L, siginfo->ctx, index, "attempt to use a closed DKIM_SIGINFO handle");

	return siginfo;
} /* DKIM_SIGINFO_checkself() */

static DKIM_SIGINFO_State *DKIM_SIGINFO_prep(lua_State *L, int index) {
	DKIM_SIGINFO_State *siginfo;

	index = lua_absindex(L, index);

	siginfo = lua_newuserdata(L, sizeof *siginfo);
	*siginfo = DKIM_SIGINFO_initializer;
	luaL_setmetatable(L, "DKIM_SIGINFO*");

	auxL_ref(L, index, &siginfo->dkim);

	return siginfo;
} /* DKIM_SIGINFO_prep() */

static DKIM_SIGINFO_State *DKIM_SIGINFO_push(lua_State *L, int index, DKIM_SIGINFO *ctx) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_prep(L, index);

	siginfo->ctx = ctx;

	return siginfo;
} /* DKIM_SIGINFO_push() */

static int DKIM_SIGINFO_getbh(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	lua_pushinteger(L, dkim_sig_getbh(siginfo->ctx));

	return 1;
} /* DKIM_SIGINFO_getbh() */

static int DKIM_SIGINFO__gc(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = luaL_checkudata(L, 1, "DKIM_SIGINFO*");

	siginfo->ctx = NULL;
	auxL_unref(L, &siginfo->dkim);

	return 0;
} /* DKIM_SIGINFO__gc() */

static luaL_Reg DKIM_SIGINFO_methods[] = {
	{ "getbh", DKIM_SIGINFO_getbh },
	{ NULL,    NULL },
}; /* DKIM_SIGINFO_methods[] */

static luaL_Reg DKIM_SIGINFO_metamethods[] = {
	{ "__gc", &DKIM_SIGINFO__gc },
	{ NULL,   NULL },
}; /* DKIM_SIGINFO_metamethods[] */


/*
 * (DKIM *) B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static DKIM_State *DKIM_checkself(lua_State *L, int index) {
	DKIM_State *dkim = luaL_checkudata(L, index, "DKIM*");

	luaL_argcheck(L, dkim->ctx, index, "attempt to use a closed DKIM handle");

	return dkim;
} /* DKIM_checkself() */

static DKIM_State *DKIM_prep(lua_State *L, int index) {
	DKIM_State *dkim;

	index = lua_absindex(L, index);

	dkim = lua_newuserdata(L, sizeof *dkim);
	*dkim = DKIM_initializer;
	luaL_setmetatable(L, "DKIM*");

	auxL_ref(L, index, &dkim->ref.lib);
	dkim->lib = lua_touserdata(L, index);

	return dkim;
} /* DKIM_prep() */

static int DKIM_geterror(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushstring(L, dkim_geterror(dkim->ctx));

	return 1;
} /* DKIM_geterror() */

static int DKIM_getmode(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushinteger(L, dkim_getmode(dkim->ctx));

	return 1;
} /* DKIM_getmode() */

static int DKIM_get_signer(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushstring(L, (const char *)dkim_get_signer(dkim->ctx));

	return 1;
} /* DKIM_get_signer() */

static int DKIM_getid(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushstring(L, dkim_getid(dkim->ctx));

	return 1;
} /* DKIM_getid() */

static int DKIM_post_final(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_CBSTAT stat = luaL_checkinteger(L, 2);

	dkim->cb.final.stat = stat;
	dkim->cb.done |= DKIM_CB_FINAL;

	return 0;
} /* DKIM_post_final() */

static int DKIM_post_key_lookup(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_CBSTAT stat = luaL_checkinteger(L, 2);
	const char *txt = luaL_optstring(L, 3, NULL);

	lua_settop(L, 3);
	auxL_ref(L, 3, &dkim->ref.txt); /* anchor txt string */

	dkim->cb.key_lookup.stat = stat;
	dkim->cb.key_lookup.txt = txt;
	dkim->cb.done |= DKIM_CB_KEY_LOOKUP;

	return 0;
} /* DKIM_post_key_lookup() */

static int DKIM_post_prescreen(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_CBSTAT stat = luaL_checkinteger(L, 2);

	dkim->cb.prescreen.stat = stat;
	dkim->cb.done |= DKIM_CB_PRESCREEN;

	return 0;
} /* DKIM_post_prescreen() */

static int DKIM_getpending(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	int exec = dkim->cb.exec & ~dkim->cb.done;
	int i;

	if (DKIM_CB_FINAL & exec) {
		lua_pushcfunction(L, DKIM_post_final);
		auxL_getref(L, dkim->lib->final);
		lua_pushvalue(L, 1);
		lua_createtable(L, dkim->cb.final.sigcount, 0);

		for (i = 0; i < dkim->cb.final.sigcount; i++) {
			DKIM_SIGINFO_push(L, 1, dkim->cb.final.siglist[i]);
			lua_rawseti(L, -2, i + 1);
		}

		return 4;
	} else if (DKIM_CB_KEY_LOOKUP & exec) {
		lua_pushcfunction(L, DKIM_post_key_lookup);
		auxL_getref(L, dkim->lib->key_lookup);
		lua_pushvalue(L, 1);
		DKIM_SIGINFO_push(L, 1, dkim->cb.key_lookup.siginfo);

		return 4;
	} else if (DKIM_CB_PRESCREEN & exec) {
		lua_pushcfunction(L, DKIM_post_prescreen);
		auxL_getref(L, dkim->lib->prescreen);
		lua_pushvalue(L, 1);
		lua_createtable(L, dkim->cb.prescreen.sigcount, 0);

		for (i = 0; i < dkim->cb.prescreen.sigcount; i++) {
			DKIM_SIGINFO_push(L, 1, dkim->cb.prescreen.siglist[i]);
			lua_rawseti(L, -2, i + 1);
		}

		return 4;
	}

	return 0;
} /* DKIM_getpending() */ 

static int DKIM__gc(lua_State *L) {
	DKIM_State *dkim = luaL_checkudata(L, 1, "DKIM*");

	if (dkim->ctx) {
		dkim_free(dkim->ctx);
		dkim->ctx = NULL;
	}

	dkim->lib = NULL;
	auxL_unref(L, &dkim->ref.lib);
	auxL_unref(L, &dkim->ref.txt);

	return 0;
} /* DKIM__gc() */

static luaL_Reg DKIM_methods[] = {
	{ "geterror",   DKIM_geterror },
	{ "getmode",    DKIM_getmode },
	{ "get_signer", DKIM_get_signer },
	{ "getid",      DKIM_getid },
	{ "getpending", DKIM_getpending },
	{ NULL,         NULL },
}; /* DKIM_methods[] */

static luaL_Reg DKIM_metamethods[] = {
	{ "__gc", &DKIM__gc },
	{ NULL,   NULL },
}; /* DKIM_metamethods[] */


/*
 * (DKIM_LIB *) B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static DKIM_LIB_State *DKIM_LIB_checkself(lua_State *L, int index) {
	DKIM_LIB_State *lib = luaL_checkudata(L, index, "DKIM_LIB*");

	luaL_argcheck(L, lib->ctx, index, "attempt to use a closed DKIM_LIB handle");

	return lib;
} /* DKIM_LIB_checkself() */

static DKIM_LIB_State *DKIM_LIB_prep(lua_State *L) {
	DKIM_LIB_State *lib;

	lib = lua_newuserdata(L, sizeof *lib);
	*lib = DKIM_LIB_initializer;
	luaL_setmetatable(L, "DKIM*");

	return lib;
} /* DKIM_LIB_prep() */

static int DKIM_LIB_flush_cache(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);
	int n;
	
	if ((n = dkim_flush_cache(lib->ctx)) >= 0) {
		lua_pushinteger(L, n);
	} else {
		lua_pushnil(L);
	}

	return 1;
} /* DKIM_LIB_flush_cache() */

static int DKIM_LIB_getcachestats(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);
	_Bool reset = auxL_optboolean(L, 2, 0);
	unsigned int queries, hits, expired, keys;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_getcachestats(lib->ctx, &queries, &hits, &expired, &keys, reset)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushinteger(L, queries);
	lua_pushinteger(L, hits);
	lua_pushinteger(L, expired);
	lua_pushinteger(L, keys);

	return 4;
} /* DKIM_LIB_getcachestats() */

static int DKIM_LIB_libfeature(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);

	lua_pushboolean(L, dkim_libfeature(lib->ctx, luaL_checkinteger(L, 2)));

	return 1;
} /* DKIM_LIB_libfeature() */

static DKIM_CBSTAT DKIM_on_final(DKIM *_dkim, DKIM_SIGINFO **siglist, int sigcount) {
	DKIM_State *dkim;
	DKIM_CBSTAT stat;
	
	if (!(dkim = dkim_get_user_context(_dkim)))
		return DKIM_CBSTAT_ERROR;

	if (!(dkim->cb.exec & DKIM_CB_FINAL))
		goto tryagain;
	if (!(dkim->cb.done & DKIM_CB_FINAL))
		goto tryagain;
	if (dkim->cb.final.siglist != siglist)
		goto tryagain;

	stat = dkim->cb.final.stat;

	dkim->cb.final = DKIM_initializer.cb.final;
	dkim->cb.exec &= ~DKIM_CB_FINAL;
	dkim->cb.done &= ~DKIM_CB_FINAL;

	return stat;
tryagain:
	dkim->cb.final = DKIM_initializer.cb.final;
	dkim->cb.final.siglist = siglist;
	dkim->cb.final.sigcount = sigcount;

	dkim->cb.exec |= DKIM_CB_FINAL;

	return DKIM_CBSTAT_TRYAGAIN;
} /* DKIM_on_final() */

static int DKIM_LIB_set_final(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);

	luaL_checktype(L, 2, LUA_TFUNCTION);
	auxL_getref(L, lib->final); /* load previous callback */
	auxL_ref(L, 2, &lib->final); /* anchor new callback */
	dkim_set_final(lib->ctx, &DKIM_on_final);

	return 1; /* return previous callback */
} /* DKIM_LIB_set_final() */

static DKIM_CBSTAT DKIM_on_key_lookup(DKIM *_dkim, DKIM_SIGINFO *siginfo, unsigned char *buf, size_t bufsiz) {
	DKIM_State *dkim;
	DKIM_CBSTAT stat;
	
	if (!(dkim = dkim_get_user_context(_dkim)))
		return DKIM_CBSTAT_ERROR;

	if (!(dkim->cb.exec & DKIM_CB_KEY_LOOKUP))
		goto tryagain;
	if (!(dkim->cb.done & DKIM_CB_KEY_LOOKUP))
		goto tryagain;
	if (dkim->cb.key_lookup.siginfo != siginfo)
		goto tryagain;

	if (bufsiz > 0) {
		const char *txt = (dkim->cb.key_lookup.txt)? dkim->cb.key_lookup.txt : "";
		size_t len = strnlen(txt, bufsiz - 1);

		memcpy(buf, txt, len);
		buf[len] = '\0';
	}

	stat = dkim->cb.key_lookup.stat;

	dkim->cb.key_lookup = DKIM_initializer.cb.key_lookup;
	dkim->cb.exec &= ~DKIM_CB_KEY_LOOKUP;
	dkim->cb.done &= ~DKIM_CB_KEY_LOOKUP;

	return stat;
tryagain:
	dkim->cb.key_lookup = DKIM_initializer.cb.key_lookup;
	dkim->cb.key_lookup.siginfo = siginfo;

	dkim->cb.exec |= DKIM_CB_KEY_LOOKUP;

	return DKIM_CBSTAT_TRYAGAIN;
} /* DKIM_on_key_lookup() */

static int DKIM_LIB_set_key_lookup(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);

	luaL_checktype(L, 2, LUA_TFUNCTION);
	auxL_getref(L, lib->key_lookup); /* load previous callback */
	auxL_ref(L, 2, &lib->key_lookup); /* anchor new callback */
	dkim_set_key_lookup(lib->ctx, &DKIM_on_key_lookup);

	return 1; /* return previous callback */
} /* DKIM_LIB_set_key_lookup() */

static DKIM_CBSTAT DKIM_on_prescreen(DKIM *_dkim, DKIM_SIGINFO **siglist, int sigcount) {
	DKIM_State *dkim;
	DKIM_CBSTAT stat;
	
	if (!(dkim = dkim_get_user_context(_dkim)))
		return DKIM_CBSTAT_ERROR;

	if (!(dkim->cb.exec & DKIM_CB_PRESCREEN))
		goto tryagain;
	if (!(dkim->cb.done & DKIM_CB_PRESCREEN))
		goto tryagain;
	if (dkim->cb.prescreen.siglist != siglist)
		goto tryagain;

	stat = dkim->cb.prescreen.stat;

	dkim->cb.prescreen = DKIM_initializer.cb.prescreen;
	dkim->cb.exec &= ~DKIM_CB_PRESCREEN;
	dkim->cb.done &= ~DKIM_CB_PRESCREEN;

	return stat;
tryagain:
	dkim->cb.prescreen = DKIM_initializer.cb.prescreen;
	dkim->cb.prescreen.siglist = siglist;
	dkim->cb.prescreen.sigcount = sigcount;

	dkim->cb.exec |= DKIM_CB_PRESCREEN;

	return DKIM_CBSTAT_TRYAGAIN;
} /* DKIM_on_prescreen() */

static int DKIM_LIB_set_prescreen(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);

	luaL_checktype(L, 2, LUA_TFUNCTION);

	auxL_getref(L, lib->prescreen); /* load previous callback */
	auxL_ref(L, 2, &lib->prescreen); /* anchor new callback */

	dkim_set_prescreen(lib->ctx, &DKIM_on_prescreen);

	return 1; /* return previous callback */
} /* DKIM_LIB_set_prescreen() */

static int DKIM_LIB_sign(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);
	const unsigned char *id = (void *)luaL_checkstring(L, 2);
	const dkim_sigkey_t secretkey = (void *)luaL_checkstring(L, 3);
	const unsigned char *selector = (void *)luaL_checkstring(L, 4);
	const unsigned char *domain = (void *)luaL_checkstring(L, 5);
	dkim_canon_t hdrcanon_alg = luaL_optinteger(L, 6, DKIM_CANON_SIMPLE);
	dkim_canon_t bodycanon_alg = luaL_optinteger(L, 7, DKIM_CANON_SIMPLE);
	dkim_alg_t sign_alg = luaL_optinteger(L, 8, DKIM_SIGN_RSASHA256);
	ssize_t length = luaL_optinteger(L, 9, -1);
	DKIM_State *dkim;
	DKIM_STAT stat;

	dkim = DKIM_prep(L, 1);

	if (!(dkim->ctx = dkim_sign(lib->ctx, id, NULL, secretkey, selector, domain, hdrcanon_alg, bodycanon_alg, sign_alg, length, &stat)))
		return auxL_pushstat(L, stat, "~$#");

	dkim_set_user_context(dkim->ctx, dkim);

	return 1;
} /* DKIM_LIB_sign() */

static int DKIM_LIB_verify(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);
	const unsigned char *id = (void *)luaL_checkstring(L, 2);
	DKIM_State *dkim;
	DKIM_STAT stat;

	dkim = DKIM_prep(L, 1);

	if (!(dkim->ctx = dkim_verify(lib->ctx, id, NULL, &stat)))
		return auxL_pushstat(L, stat, "~$#");

	dkim_set_user_context(dkim->ctx, dkim);

	return 1;
} /* DKIM_LIB_verify() */

static int DKIM_LIB__gc(lua_State *L) {
	DKIM_LIB_State *lib = luaL_checkudata(L, 1, "DKIM_LIB*");

	if (lib->ctx) {
		dkim_close(lib->ctx);
		lib->ctx = NULL;
	}

	auxL_unref(L, &lib->final);
	auxL_unref(L, &lib->key_lookup);
	auxL_unref(L, &lib->prescreen);

	return 0;
} /* DKIM_LIB__gc() */

static luaL_Reg DKIM_LIB_methods[] = {
	{ "flush_cache",    DKIM_LIB_flush_cache },
	{ "getcachestats",  DKIM_LIB_getcachestats },
	{ "libfeature",     DKIM_LIB_libfeature },
	{ "set_final",      DKIM_LIB_set_final },
	{ "set_key_lookup", DKIM_LIB_set_key_lookup },
	{ "set_prescreen",  DKIM_LIB_set_prescreen },
	{ "sign",           DKIM_LIB_sign },
	{ "verify",         DKIM_LIB_verify },
	{ NULL,             NULL },
}; /* DKIM_LIB_methods[] */

static luaL_Reg DKIM_LIB_metamethods[] = {
	{ "__gc",   DKIM_LIB__gc },
	{ NULL,     NULL },
}; /* DKIM_LIB_metamethods[] */


/*
 * O P E N D K I M  T O P - L E V E L  B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static int opendkim_init(lua_State *L) {
	DKIM_LIB_State *lib;

	lua_settop(L, 1);

	lib = DKIM_LIB_prep(L);

	if (!(lib->ctx = dkim_init(NULL, NULL)))
		return auxL_pusherror(L, ENOMEM, "~$#");

	return 1;
} /* opendkim_init() */

static int opendkim_libversion(lua_State *L) {
	lua_pushinteger(L, dkim_libversion());

	return 1;
} /* opendkim_libversion() */

static int opendkim_ssl_version(lua_State *L) {
	lua_pushinteger(L, dkim_ssl_version());

	return 1;
} /* opendkim_ssl_version() */

static luaL_Reg opendkim_globals[] = {
	{ "init",        opendkim_init },
	{ "libversion",  opendkim_libversion },
	{ "ssl_version", opendkim_ssl_version },
	{ NULL,          NULL },
}; /* opendkim_globals[] */

int luaopen_opendkim_core(lua_State *L) {
	auxL_newmetatable(L, "DKIM_LIB*", DKIM_LIB_methods, DKIM_LIB_metamethods, 0);
	lua_pop(L, 1);

	auxL_newmetatable(L, "DKIM*", DKIM_methods, DKIM_metamethods, 0);
	lua_pop(L, 1);

	auxL_newmetatable(L, "DKIM_SIGINFO*", DKIM_SIGINFO_methods, DKIM_SIGINFO_metamethods, 0);
	lua_pop(L, 1);

	luaL_newlib(L, opendkim_globals);

	return 1;
} /* luaopen_opendkim_core() */
