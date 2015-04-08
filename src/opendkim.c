/* ==========================================================================
 * opendkim.c - Lua bindings to OpenDKIM's libopendkim API.
 * --------------------------------------------------------------------------
 * Copyright (c) 2015 Barracuda Networks, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#include <stdlib.h> /* free(3) */
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

#if 0 /* not needed yet */
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
#endif

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

#define AUX_MAX(a, b) (((a) > (b))? (a) : (b))

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

static _Bool auxL_checkboolean(lua_State *L, int index) {
	luaL_checkany(L, index);

	return lua_toboolean(L, index);
} /* auxL_checkboolean() */

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

static int auxL_checkcbstat(lua_State *L, int index) {
	DKIM_CBSTAT error = luaL_checkinteger(L, index);

	/* FIXME: Add constraint check. */

	return error;
} /* auxL_checkcbstat() */


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

static DKIM_State *DKIM_checkself(lua_State *L, int index);
static DKIM_State *DKIM_checkref(lua_State *L, auxref_t ref);

typedef struct {
	DKIM_SIGINFO *ctx;
	auxref_t dkim;
} DKIM_SIGINFO_State;

static const DKIM_SIGINFO_State DKIM_SIGINFO_initializer = {
	.dkim = LUA_NOREF,
}; /* DKIM_SIGINFO_initializer */

static DKIM_SIGINFO_State *DKIM_SIGINFO_checkself(lua_State *L, int index);

typedef struct {
	DKIM_QUERYINFO *ctx;
} DKIM_QUERYINFO_State;

static const DKIM_QUERYINFO_State DKIM_QUERYINFO_initializer = { 0 };


/*
 * (DKIM_QUERYINFO *) B I N D I N G S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static DKIM_QUERYINFO_State *DKIM_QUERYINFO_checkself(lua_State *L, int index) {
	DKIM_QUERYINFO_State *siginfo = luaL_checkudata(L, index, "DKIM_QUERYINFO*");

	luaL_argcheck(L, siginfo->ctx, index, "attempt to use a closed DKIM_QUERYINFO handle");

	return siginfo;
} /* DKIM_QUERYINFO_checkself() */

static DKIM_QUERYINFO_State *DKIM_QUERYINFO_prep(lua_State *L) {
	DKIM_QUERYINFO_State *qryinfo;

	qryinfo = lua_newuserdata(L, sizeof *qryinfo);
	*qryinfo = DKIM_QUERYINFO_initializer;
	luaL_setmetatable(L, "DKIM_QUERYINFO*");

	return qryinfo;
} /* DKIM_QUERYINFO_prep() */

static int DKIM_QUERYINFO_getname(lua_State *L) {
	DKIM_QUERYINFO_State *qryinfo = DKIM_QUERYINFO_checkself(L, 1);

	lua_pushstring(L, dkim_qi_getname(qryinfo->ctx));

	return 1;
} /* DKIM_QUERYINFO_getname() */

static int DKIM_QUERYINFO_gettype(lua_State *L) {
	DKIM_QUERYINFO_State *qryinfo = DKIM_QUERYINFO_checkself(L, 1);
	int type;

	if (-1 == (type = dkim_qi_gettype(qryinfo->ctx)))
		return 0;

	lua_pushinteger(L, type);

	return 1;
} /* DKIM_QUERYINFO_gettype() */

static int DKIM_QUERYINFO__gc(lua_State *L) {
	DKIM_QUERYINFO_State *qryinfo = luaL_checkudata(L, 1, "DKIM_QUERYINFO*");

	free(qryinfo->ctx);
	qryinfo->ctx = NULL;

	return 0;
} /* DKIM_QUERYINFO__gc() */

static luaL_Reg DKIM_QUERYINFO_methods[] = {
	{ "getname", &DKIM_QUERYINFO_getname },
	{ "gettype", &DKIM_QUERYINFO_gettype },
	{ NULL, NULL },
}; /* DKIM_QUERYINFO_methods[] */

static luaL_Reg DKIM_QUERYINFO_metamethods[] = {
	{ "__gc", &DKIM_QUERYINFO__gc },
	{ NULL,   NULL },
}; /* DKIM_QUERYINFO_metamethods[] */


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

static int DKIM_SIGINFO_getcanonlen(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	DKIM_State *dkim = DKIM_checkref(L, siginfo->dkim);
	ssize_t msglen, canonlen, signlen;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_getcanonlen(dkim->ctx, siginfo->ctx, &msglen, &canonlen, &signlen)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushinteger(L, msglen);
	lua_pushinteger(L, canonlen);
	lua_pushinteger(L, signlen);

	return 3;
} /* DKIM_SIGINFO_getcanonlen() */

static int DKIM_SIGINFO_getcanons(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	dkim_canon_t hdr, body;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_getcanons(siginfo->ctx, &hdr, &body)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushinteger(L, hdr);
	lua_pushinteger(L, body);

	return 2;
} /* DKIM_SIGINFO_getcanons() */

static int DKIM_SIGINFO_getdnssec(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	lua_pushinteger(L, dkim_sig_getdnssec(siginfo->ctx));

	return 1;
} /* DKIM_SIGINFO_getdnssec() */

static int DKIM_SIGINFO_getdomain(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	const void *domain = dkim_sig_getdomain(siginfo->ctx);

	lua_pushstring(L, domain);

	return 1;
} /* DKIM_SIGINFO_getdomain() */

static int DKIM_SIGINFO_geterror(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	lua_pushinteger(L, dkim_sig_geterror(siginfo->ctx));

	return 1;
} /* DKIM_SIGINFO_geterror() */

static int DKIM_SIGINFO_getflags(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	lua_pushinteger(L, dkim_sig_getflags(siginfo->ctx));

	return 1;
} /* DKIM_SIGINFO_getflags() */

static int DKIM_SIGINFO_getidentity_(lua_State *L, DKIM_State *dkim, DKIM_SIGINFO_State *siginfo) {
	unsigned char buf_[256];
	void *buf;
	size_t bufsiz;
	int top;
	DKIM_STAT stat;

	top = lua_gettop(L);
	buf = buf_;
	bufsiz = sizeof buf;

	goto again;

	do {
		if (SIZE_MAX / 2 < bufsiz)
			return auxL_pushstat(L, DKIM_STAT_NORESOURCE, "~$#");

		lua_settop(L, top);
		bufsiz *= 2;
		buf = lua_newuserdata(L, bufsiz);
again:
		stat = dkim_sig_getidentity(dkim->ctx, siginfo->ctx, buf, bufsiz);
	} while (stat == DKIM_STAT_NORESOURCE);

	if (stat != DKIM_STAT_OK)
		return auxL_pushstat(L, stat, "~$#");

	lua_pushlstring(L, buf, strnlen(buf, bufsiz));

	return 1;
} /* DKIM_SIGINFO_getidentity_() */

static int DKIM_SIGINFO_getidentity(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	DKIM_State *dkim = DKIM_checkref(L, siginfo->dkim);

	return DKIM_SIGINFO_getidentity_(L, dkim, siginfo);
} /* DKIM_SIGINFO_getidentity() */

static int DKIM_SIGINFO_getqueries_(lua_State *L, DKIM_State *dkim, DKIM_SIGINFO_State *siginfo) {
	DKIM_QUERYINFO **list = NULL;
	unsigned int n = 0, i;
	DKIM_STAT stat;
	int tindex;

	lua_newtable(L);
	tindex = lua_gettop(L);

	do {
		/* free any previous list */
		for (i = 0; i < n; i++)
			free(list[i]);
		free(list);
		list = NULL;

		/* reset stack */
		lua_settop(L, tindex);

		/* pre-allocate objects */
		luaL_checkstack(L, MAX(n, 1), NULL);
		for (i = 0; i < MAX(n, 1); i++)
			DKIM_QUERYINFO_prep(L);

		if (DKIM_STAT_OK != (stat = dkim_sig_getqueries(dkim->ctx, siginfo->ctx, &list, &n)))
			return auxL_pushstat(L, stat, "~$#");
	} while ((size_t)(lua_gettop(L) - tindex) < n);

	/* shrink stack if n is less than what we pre-allocated */
	lua_settop(L, tindex + n);

	for (i = 0; i < n; i++) {
		DKIM_QUERYINFO_State *qryinfo = lua_touserdata(L, tindex + i + 1);
		qryinfo->ctx = list[i];
		list[i] = NULL;
	}

	free(list);
	list = NULL;

	/* move objects to table */
	for (i = 1; lua_gettop(L) > tindex; i++) {
		lua_rawseti(L, tindex, i);
	}

	/* sanity check our return value */
	luaL_checktype(L, -1, LUA_TTABLE);

	return 1;
} /* DKIM_SIGINFO_getqueries_() */

static int DKIM_SIGINFO_getqueries(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	DKIM_State *dkim = DKIM_checkref(L, siginfo->dkim);

	return DKIM_SIGINFO_getqueries_(L, dkim, siginfo);
} /* DKIM_SIGINFO_getqueries() */

static int DKIM_SIGINFO_getkeysize(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	unsigned int bits;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_getkeysize(siginfo->ctx, &bits)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushinteger(L, bits);

	return 1;
} /* DKIM_SIGINFO_getkeysize() */

static int DKIM_SIGINFO_getreportinfo_(lua_State *L, DKIM_State *dkim, DKIM_SIGINFO_State *siginfo) {
	(void)L; (void)dkim; (void)siginfo;

	/*
	 * NB: dkim_sig_getreportinfo doesn't appear to support asynchronous
	 * DNS. We may need to re-implement this routine in Lua.
	 */

	return 0;
} /* DKIM_SIGINFO_getreportinfo_() */

static int DKIM_SIGINFO_getreportinfo(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	DKIM_State *dkim = DKIM_checkref(L, siginfo->dkim);

	return DKIM_SIGINFO_getidentity_(L, dkim, siginfo);
} /* DKIM_SIGINFO_getreportinfo() */

static int DKIM_SIGINFO_getselector(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	const void *selector = dkim_sig_getselector(siginfo->ctx);

	lua_pushstring(L, selector);

	return 1;
} /* DKIM_SIGINFO_getselector() */

static int DKIM_SIGINFO_getsignalg(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	dkim_alg_t alg;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_getsignalg(siginfo->ctx, &alg)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushinteger(L, alg);

	return 1;
} /* DKIM_SIGINFO_getsignalg() */

static int DKIM_SIGINFO_getsslbuf(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	lua_pushstring(L, dkim_sig_getsslbuf(siginfo->ctx));

	return 1;
} /* DKIM_SIGINFO_getsslbuf() */

static int DKIM_SIGINFO_hdrsigned(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	unsigned char *hdr = (void *)luaL_checkstring(L, 2);

	lua_pushboolean(L, dkim_sig_hdrsigned(siginfo->ctx, hdr));

	return 1;
} /* DKIM_SIGINFO_hdrsigned() */

static int DKIM_SIGINFO_ignore(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	dkim_sig_ignore(siginfo->ctx);

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_SIGINFO_ignore() */

static int DKIM_SIGINFO_process_(lua_State *L, DKIM_State *dkim, DKIM_SIGINFO_State *siginfo) {
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_process(dkim->ctx, siginfo->ctx)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_SIGINFO_process_() */

static int DKIM_SIGINFO_process(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	DKIM_State *dkim = DKIM_checkref(L, siginfo->dkim);

	return DKIM_SIGINFO_process_(L, dkim, siginfo);
} /* DKIM_SIGINFO_process() */

static int DKIM_SIGINFO_setdnssec(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	int status = luaL_checkinteger(L, 2);

	dkim_sig_setdnssec(siginfo->ctx, status);

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_SIGINFO_setdnssec() */

static int DKIM_SIGINFO_seterror(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	int error = luaL_checkinteger(L, 2);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_seterror(siginfo->ctx, error)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_SIGINFO_seterror() */

static int DKIM_SIGINFO_getowner(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);

	auxL_getref(L, siginfo->dkim);
	DKIM_checkself(L, -1);

	return 1;
} /* DKIM_SIGINFO_getowner() */

static int DKIM_SIGINFO_get_sigsubstring_(lua_State *L, DKIM_State *dkim, DKIM_SIGINFO_State *siginfo) {
	unsigned char buf_[256];
	void *buf;
	size_t bufsiz;
	int top;
	DKIM_STAT stat;

	top = lua_gettop(L);
	buf = buf_;
	bufsiz = sizeof buf;

	goto again;

	do {
		if (SIZE_MAX / 2 < bufsiz)
			return auxL_pushstat(L, DKIM_STAT_NORESOURCE, "~$#");

		lua_settop(L, top);
		bufsiz *= 2;
		buf = lua_newuserdata(L, bufsiz);
again:
		stat = dkim_get_sigsubstring(dkim->ctx, siginfo->ctx, buf, &bufsiz);
	} while (stat == DKIM_STAT_NORESOURCE);

	if (stat != DKIM_STAT_OK)
		return auxL_pushstat(L, stat, "~$#");

	lua_pushlstring(L, buf, strnlen(buf, bufsiz));

	return 1;
} /* DKIM_SIGINFO_get_sigsubstring_() */

static int DKIM_SIGINFO_get_sigsubstring(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	DKIM_State *dkim = DKIM_checkref(L, siginfo->dkim);

	return DKIM_SIGINFO_get_sigsubstring_(L, dkim, siginfo);
} /* DKIM_SIGINFO_get_sigsubstring() */

static int DKIM_SIGINFO_gethashes(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 1);
	void *hh = NULL, *bh = NULL;
	size_t hhlen = 0, bhlen = 0;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_gethashes(siginfo->ctx, &hh, &hhlen, &bh, &bhlen)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushlstring(L, hh, hhlen);
	lua_pushlstring(L, bh, bhlen);

	return 2;
} /* DKIM_SIGINFO_gethashes() */

static int DKIM_SIGINFO__gc(lua_State *L) {
	DKIM_SIGINFO_State *siginfo = luaL_checkudata(L, 1, "DKIM_SIGINFO*");

	siginfo->ctx = NULL;
	auxL_unref(L, &siginfo->dkim);

	return 0;
} /* DKIM_SIGINFO__gc() */

static luaL_Reg DKIM_SIGINFO_methods[] = {
	{ "getbh", DKIM_SIGINFO_getbh },
	{ "getcanonlen", DKIM_SIGINFO_getcanonlen },
	{ "getcanons", DKIM_SIGINFO_getcanons },
	{ "getdnssec", DKIM_SIGINFO_getdnssec },
	{ "getdomain", DKIM_SIGINFO_getdomain },
	{ "geterror", DKIM_SIGINFO_geterror },
	{ "getflags", DKIM_SIGINFO_getflags },
	{ "getidentity", DKIM_SIGINFO_getidentity },
	{ "getkeysize", DKIM_SIGINFO_getkeysize },
	{ "getqueries", DKIM_SIGINFO_getqueries },
	{ "getreportinfo", DKIM_SIGINFO_getreportinfo },
	{ "getselector", DKIM_SIGINFO_getselector },
	{ "getsignalg", DKIM_SIGINFO_getsignalg },
	{ "getsslbuf", DKIM_SIGINFO_getsslbuf },
	{ "hdrsigned", DKIM_SIGINFO_hdrsigned },
	{ "ignore", DKIM_SIGINFO_ignore },
	{ "process", DKIM_SIGINFO_process },
	{ "setdnssec", DKIM_SIGINFO_setdnssec },
	{ "seterror", DKIM_SIGINFO_seterror },
	{ "get_sigsubstring", DKIM_SIGINFO_get_sigsubstring },
	{ "gethashes", DKIM_SIGINFO_gethashes },

	/* auxiliary module routines */
	{ "getowner", DKIM_SIGINFO_getowner },
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

static DKIM_State *DKIM_checkref(lua_State *L, auxref_t ref) {
	DKIM_State *dkim;

	auxL_getref(L, ref);
	dkim = DKIM_checkself(L, -1);
	lua_pop(L, 1);

	return dkim;
} /* DKIM_checkref() */

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

static int DKIM_add_querymethod(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	const char *method = luaL_checkstring(L, 2);
	const char *options = luaL_checkstring(L, 3);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_add_querymethod(dkim->ctx, method, options)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_add_querymethod() */

static int DKIM_add_xtag(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	const char *tag = luaL_checkstring(L, 2);
	const char *value = luaL_checkstring(L, 3);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_add_xtag(dkim->ctx, tag, value)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_add_xtag() */

static int DKIM_getpartial(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushboolean(L, dkim_getpartial(dkim->ctx));

	return 1;
} /* DKIM_getpartial() */

static int DKIM_getsighdr(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	size_t initial = luaL_optinteger(L, 2, strlen(DKIM_SIGNHEADER) + 2);
	unsigned char *hdr = NULL;
	size_t len = 0;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_getsighdr_d(dkim->ctx, initial, &hdr, &len)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushlstring(L, (char *)hdr, len);

	return 1;
} /* DKIM_getsighdr() */

static int DKIM_privkey_load(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_privkey_load(dkim->ctx)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_privkey_load() */

static int DKIM_set_margin(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	int margin = luaL_optinteger(L, 2, DKIM_HDRMARGIN);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_set_margin(dkim->ctx, margin)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_set_margin() */

static int DKIM_set_signer(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	const void *signer = luaL_checkstring(L, 2);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_set_signer(dkim->ctx, signer)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_set_signer() */

static int DKIM_setpartial(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	_Bool value = auxL_checkboolean(L, 2);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_setpartial(dkim->ctx, value)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_setpartial() */

static int DKIM_signhdrs(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	const char **hdrlist;
	size_t i, n;
	DKIM_STAT stat;

	luaL_checktype(L, 2, LUA_TTABLE);
	n = lua_rawlen(L, 2);

	hdrlist = lua_newuserdata(L, sizeof *hdrlist * (n + 1));

	for (i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		hdrlist[i] = luaL_checkstring(L, -1);
		lua_pop(L, 1);
	}

	hdrlist[n] = NULL;

	if (DKIM_STAT_OK != (stat = dkim_signhdrs(dkim->ctx, hdrlist)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_signhdrs() */

static int DKIM_getsiglist(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO **siglist = NULL;
	int sigcount = 0, i;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_getsiglist(dkim->ctx, &siglist, &sigcount)))
		return auxL_pushstat(L, stat, "~$#");

	lua_createtable(L, sigcount, 0);

	for (i = 0; i < sigcount; i++) {
		DKIM_SIGINFO_push(L, 1, siglist[i]);
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
} /* DKIM_getsiglist() */

static int DKIM_getsignature(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO *siginfo;

	if (!(siginfo = dkim_getsignature(dkim->ctx)))
		return 0;

	DKIM_SIGINFO_push(L, 1, siginfo);

	return 1;
} /* DKIM_getsignature() */

static int DKIM_getsslbuf(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushstring(L, dkim_getsslbuf(dkim->ctx));

	return 1;
} /* DKIM_getsslbuf() */

static int DKIM_getuser(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushstring(L, (const char *)dkim_getuser(dkim->ctx));

	return 1;
} /* DKIM_getuser() */

static int DKIM_minbody(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushinteger(L, dkim_minbody(dkim->ctx));

	return 1;
} /* DKIM_minbody() */

static int DKIM_ohdrs(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = (lua_isnoneornil(L, 2))? NULL : DKIM_SIGINFO_checkself(L, 2);
	unsigned char **hdrlist;
	int hdrlimit = (128 / 2), hdrcount, i;
	DKIM_STAT stat;

	/* loop, doubling the array size until the list fits */
	do {
		lua_settop(L, 2);

		if (INT_MAX / 2 < hdrlimit)
			goto overflow;

		hdrlimit *= 2;

		if (SIZE_MAX / sizeof *hdrlist < (size_t)hdrlimit)
			goto overflow;

		hdrlist = lua_newuserdata(L, sizeof *hdrlist * hdrlimit);

		hdrcount = hdrlimit;
		if (DKIM_STAT_OK != (stat = dkim_ohdrs(dkim->ctx, siginfo->ctx, hdrlist, &hdrcount)))
			return auxL_pushstat(L, stat, "~$#");
	} while (hdrcount > hdrlimit);

	lua_createtable(L, hdrcount, 0);

	for (i = 0; i < hdrcount; i++) {
		lua_pushstring(L, (char *)hdrlist[i]);
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
overflow:
	return auxL_pushstat(L, DKIM_STAT_NORESOURCE, "~$#");
} /* DKIM_ohdrs() */

static int DKIM_sig_getcanonlen(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 2);
	ssize_t msglen, canonlen, signlen;
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_sig_getcanonlen(dkim->ctx, siginfo->ctx, &msglen, &canonlen, &signlen)))
		return auxL_pushstat(L, stat, "~$#");

	lua_pushinteger(L, msglen);
	lua_pushinteger(L, canonlen);
	lua_pushinteger(L, signlen);

	return 3;
} /* DKIM_sig_getcanonlen() */

static int DKIM_sig_getidentity(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = (lua_isnoneornil(L, 2))? NULL : DKIM_SIGINFO_checkself(L, 2);

	return DKIM_SIGINFO_getidentity_(L, dkim, siginfo);
} /* DKIM_sig_getidentity() */

static int DKIM_sig_getqueries(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 2);

	return DKIM_SIGINFO_getqueries_(L, dkim, siginfo);
} /* DKIM_sig_getqueries() */

static int DKIM_sig_getreportinfo(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 2);

	return DKIM_SIGINFO_getreportinfo_(L, dkim, siginfo);
} /* DKIM_sig_getreportinfo() */

static int DKIM_sig_process(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 2);

	return DKIM_SIGINFO_process_(L, dkim, siginfo);
} /* DKIM_sig_process() */

static int DKIM_header(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	void *hdr;
	size_t len;
	DKIM_STAT stat;

	hdr = (void *)luaL_checklstring(L, 2, &len);

	if (DKIM_STAT_OK != (stat = dkim_header(dkim->ctx, hdr, len)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_header() */

static int DKIM_eoh(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_STAT stat;

	if (DKIM_STAT_OK != (stat = dkim_eoh(dkim->ctx)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_eoh() */

static int DKIM_body(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	void *body;
	size_t len;
	DKIM_STAT stat;

	body = (void *)luaL_checklstring(L, 2, &len);

	if (DKIM_STAT_OK != (stat = dkim_body(dkim->ctx, body, len)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_body() */

static int DKIM_eom(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_STAT stat;
	_Bool testkey;

	if (DKIM_STAT_OK != (stat = dkim_eom(dkim->ctx, &testkey)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);
	lua_pushboolean(L, testkey);

	return 2;
} /* DKIM_eom() */

static int DKIM_chunk(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	void *chunk;
	size_t len;
	DKIM_STAT stat;

	chunk = (void *)luaL_optlstring(L, 2, NULL, &len);

	if (DKIM_STAT_OK != (stat = dkim_chunk(dkim->ctx, chunk, len)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_chunk() */

static int DKIM_getid(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);

	lua_pushstring(L, dkim_getid(dkim->ctx));

	return 1;
} /* DKIM_getid() */

#if 0 /* not implemented (documentation out of date) */
static int DKIM_get_msgdate(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	uint64_t ts;

	if (0 == (ts = dkim_get_msgdate(dkim->ctx))) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "Date: header field was not found, or one was found but could not be parsed, or the feature was not enabled when the DKIM library was compiled");

		return 2;
	}

	lua_pushnumber(L, ts);

	return 1;
} /* DKIM_get_msgdate() */
#endif

static int DKIM_get_sigsubstring(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_SIGINFO_State *siginfo = DKIM_SIGINFO_checkself(L, 2);

	return DKIM_SIGINFO_get_sigsubstring_(L, dkim, siginfo);
} /* DKIM_get_sigsubstring() */

static int DKIM_key_syntax(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	const char *src;
	size_t len;
	unsigned char *buf;
	DKIM_STAT stat;

	src = luaL_checklstring(L, 2, &len);
	buf = lua_newuserdata(L, len + 1);
	memcpy(buf, src, len + 1);

	if (DKIM_STAT_OK != (stat = dkim_key_syntax(dkim->ctx, buf, len)))
		return auxL_pushstat(L, stat, "0$#");

	lua_pushboolean(L, 1);

	return 1;
} /* DKIM_key_syntax() */

static int DKIM_post_final(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_CBSTAT stat = auxL_checkcbstat(L, 2);

	dkim->cb.final.stat = stat;
	dkim->cb.done |= DKIM_CB_FINAL;

	return 0;
} /* DKIM_post_final() */

static int DKIM_post_key_lookup(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	const char *txt = NULL;
	DKIM_CBSTAT stat;

	lua_settop(L, 2);

	if (lua_type(L, 2) == LUA_TSTRING) {
		stat = DKIM_CBSTAT_CONTINUE;

		/* XXX: Detect embedded NULs in the txt record? */
		txt = luaL_checkstring(L, 2);
		auxL_ref(L, 2, &dkim->ref.txt); /* anchor txt string */
	} else {
		stat = auxL_checkcbstat(L, 2);
	}

	dkim->cb.key_lookup.stat = stat;
	dkim->cb.key_lookup.txt = txt;
	dkim->cb.done |= DKIM_CB_KEY_LOOKUP;

	return 0;
} /* DKIM_post_key_lookup() */

static int DKIM_post_prescreen(lua_State *L) {
	DKIM_State *dkim = DKIM_checkself(L, 1);
	DKIM_CBSTAT stat = auxL_checkcbstat(L, 2);

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
	/* administration methods */
	{ "geterror", DKIM_geterror },
	{ "getmode", DKIM_getmode },
	{ "get_signer", DKIM_get_signer },

	/* signing methods */
	{ "add_querymethod", DKIM_add_querymethod },
	{ "add_xtag", DKIM_add_xtag },
	{ "getpartial", DKIM_getpartial },
	{ "getsighdr", DKIM_getsighdr },
	{ "privkey_load", DKIM_privkey_load },
	{ "set_margin", DKIM_set_margin },
	{ "set_signer", DKIM_set_signer },
	{ "setpartial", DKIM_setpartial },
	{ "signhdrs", DKIM_signhdrs },

	/* verification methods */
#if 0 /* does not support asynchronous DNS */
	{ "atps_check", DKIM_atps_check },
#endif
#if 0 /* not necessary */
	{ "diffheaders", DKIM_diffheaders },
#endif
#if 0 /* experimental; documented but not actually implemented in OpenDKIM */
	{ "get_reputation", DKIM_dkim_get_reputation },
#endif
	{ "getsiglist", DKIM_getsiglist },
	{ "getsignature", DKIM_getsignature },
	{ "getsslbuf", DKIM_getsslbuf },
	{ "getuser", DKIM_getuser },
	{ "minbody", DKIM_minbody },
	{ "ohdrs", DKIM_ohdrs },
	{ "sig_getcanonlen", DKIM_sig_getcanonlen },
	{ "sig_getidentity", DKIM_sig_getidentity },
	{ "sig_getqueries", DKIM_sig_getqueries },
	{ "sig_getreportinfo", DKIM_sig_getreportinfo },
	{ "sig_process", DKIM_sig_process },

	/* processing methods */
	{ "header", DKIM_header },
	{ "eoh", DKIM_eoh },
	{ "body", DKIM_body },
	{ "eom", DKIM_eom },
	{ "chunk", DKIM_chunk },

	/* utility methods */
	{ "getid", DKIM_getid },
#if 0 /* not implemented (documentation out of date) */
	{ "get_msgdate", DKIM_get_msgdate },
#endif
	{ "get_sigsubstring", DKIM_get_sigsubstring },
	{ "key_syntax", DKIM_key_syntax },

	/* module auxiliary routines */
	{ "getpending", DKIM_getpending },
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

static DKIM_LIB_State *DKIM_LIB_checkself(lua_State *L, int index) {
	DKIM_LIB_State *lib = luaL_checkudata(L, index, "DKIM_LIB*");

	luaL_argcheck(L, lib->ctx, index, "attempt to use a closed DKIM_LIB handle");

	return lib;
} /* DKIM_LIB_checkself() */

static DKIM_LIB_State *DKIM_LIB_prep(lua_State *L) {
	DKIM_LIB_State *lib;

	lib = lua_newuserdata(L, sizeof *lib);
	*lib = DKIM_LIB_initializer;
	luaL_setmetatable(L, "DKIM_LIB*");

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

#define AUX_DKIM_OPTIONS(...) do { \
	if (DKIM_STAT_OK != (stat = dkim_options(lib->ctx, op, opt, __VA_ARGS__))) \
		goto error; \
} while (0)

static int DKIM_LIB_options(lua_State *L) {
	DKIM_LIB_State *lib = DKIM_LIB_checkself(L, 1);
	int op = luaL_checkinteger(L, 2);
	int opt = luaL_checkinteger(L, 3);
	DKIM_STAT stat;

	luaL_argcheck(L, op == DKIM_OP_GETOPT || op == DKIM_OP_SETOPT, 2, "must be either DKIM_OP_GETOPT or DKIM_OP_SETOPT");

	switch (opt) {
	case DKIM_OPTS_CLOCKDRIFT:
		/* FALL THROUGH */
	case DKIM_OPTS_FIXEDTIME:
		/* FALL THROUGH */
	case DKIM_OPTS_SIGNATURETTL: {
		uint64_t seconds;

		if (op == DKIM_OP_GETOPT) {
			AUX_DKIM_OPTIONS(&seconds, sizeof seconds);
			lua_pushnumber(L, seconds);

			return 1;
		} else {
			seconds = luaL_checkinteger(L, 4);
			AUX_DKIM_OPTIONS(&seconds, sizeof seconds);
		}

		break;
	}
	case DKIM_OPTS_FLAGS:
		/* FALL THROUGH */
	case DKIM_OPTS_MINKEYBITS:
		/* FALL THROUGH */
	case DKIM_OPTS_TIMEOUT: {
		unsigned int value;

		if (op == DKIM_OP_GETOPT) {
			AUX_DKIM_OPTIONS(&value, sizeof value);
			lua_pushinteger(L, value);

			return 1;
		} else {
			value = luaL_checkinteger(L, 4);
			AUX_DKIM_OPTIONS(&value, sizeof value);
		}

		break;
	}
	case DKIM_OPTS_MUSTBESIGNED:
		/* FALL THROUGH */
	case DKIM_OPTS_OVERSIGNHDRS:
		/* FALL THROUGH */
	case DKIM_OPTS_REQUIREDHDRS:
		/* FALL THROUGH */
	case DKIM_OPTS_SIGNHDRS:
		/* FALL THROUGH */
	case DKIM_OPTS_SKIPHDRS: {
		const char **list;
		size_t i, n, m;

		if (op == DKIM_OP_GETOPT) {
			AUX_DKIM_OPTIONS(&list, sizeof list);

			lua_newtable(L);

			for (i = 0; i < INT_MAX - 1 && list[i]; i++) {
				lua_pushstring(L, list[i]);
				lua_rawseti(L, -2, i + 1);
			}

			return 1;
		} else if (lua_isnil(L, 4)) {
			AUX_DKIM_OPTIONS(NULL, 0);
		} else {
			luaL_checktype(L, 4, LUA_TTABLE);
			n = lua_rawlen(L, 4);
			m = n + 1;

			if (m == 0 || m > INT_MAX || SIZE_MAX / sizeof *list < m)
				return auxL_pusherror(L, EOVERFLOW, "~$#");

			list = lua_newuserdata(L, sizeof *list * m);

			for (i = 0; i < n; i++) {
				lua_rawgeti(L, 4, i + 1);
				list[i] = luaL_checkstring(L, -1);
				lua_pop(L, 1);
			}

			list[i] = NULL;

			AUX_DKIM_OPTIONS(list, sizeof list);
		}

		break;
	}
	case DKIM_OPTS_QUERYINFO: {
		if (op == DKIM_OP_GETOPT) {
			char dst[256];

			AUX_DKIM_OPTIONS(dst, sizeof dst);
			lua_pushstring(L, dst);

			return 1;
		} else {
			size_t len;
			const char *src = luaL_checklstring(L, 4, &len);

			AUX_DKIM_OPTIONS((void *)src, len);
		}

		break;
	}
	case DKIM_OPTS_QUERYMETHOD: {
		dkim_query_t value;

		if (op == DKIM_OP_GETOPT) {
			AUX_DKIM_OPTIONS(&value, sizeof value);
			lua_pushinteger(L, value);

			return 1;
		} else {
			value = luaL_checkinteger(L, 4);
			AUX_DKIM_OPTIONS(&value, sizeof value);
		}

		break;
	}
	case DKIM_OPTS_TMPDIR: {
		if (op == DKIM_OP_GETOPT) {
			char dst[256];

			AUX_DKIM_OPTIONS(dst, sizeof dst);
			lua_pushstring(L, dst);

			return 1;
		} else {
			size_t len;
			const char *src = luaL_optlstring(L, 4, NULL, &len);

			AUX_DKIM_OPTIONS((void *)src, len);
		}

		break;
	}
	default:
		return luaL_argerror(L, 3, lua_pushfstring(L, "%d: invalid option", opt));
	}

	lua_pushboolean(L, 1);

	return 1;
error:
	return auxL_pushstat(L, stat, "~$#");
} /* DKIM_LIB_options() */

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
	{ "options",        DKIM_LIB_options },
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

static int opendkim_getresultstr(lua_State *L) {
	lua_pushstring(L, dkim_getresultstr(luaL_checkinteger(L, 1)));

	return 1;
} /* opendkim_getresultstr() */

static int opendkim_sig_geterrorstr(lua_State *L) {
	lua_pushstring(L, dkim_sig_geterrorstr(luaL_checkinteger(L, 1)));

	return 1;
} /* opendkim_sig_geterrorstr() */

static int opendkim_mail_parse(lua_State *L) {
	const char *src;
	size_t len;
	unsigned char *buf, *user, *domain;

	src = luaL_checklstring(L, 1, &len);
	buf = lua_newuserdata(L, len + 1);
	memcpy(buf, src, len + 1);

	if (0 != dkim_mail_parse(buf, &user, &domain))
		return auxL_pushstat(L, DKIM_STAT_INTERNAL, "~$#");

	lua_pushstring(L, (char *)user);
	lua_pushstring(L, (char *)domain);

	return 2;
} /* opendkim_mail_parse() */

#if 0 /* not declared in dkim.h */
static int opendkim_mail_parse_multi(lua_State *L) {
	const char *src;
	size_t len;
	unsigned char *buf, *user, *domain;

	src = luaL_checklstring(L, 1, &len);
	buf = lua_newuserdata(L, len + 1);
	memcpy(buf, src, len + 1);

	if (0 != dkim_mail_parse_multi(buf, &user, &domain))
		return auxL_pushstat(L, DKIM_STAT_INTERNAL, "~$#");

	lua_pushstring(L, (char *)user);
	lua_pushstring(L, (char *)domain);

	return 2;
} /* opendkim_mail_parse_multi() */
#endif

static int opendkim_interpose(lua_State *L) {
	lua_settop(L, 3);

	luaL_checktype(L, 1, LUA_TSTRING);
	luaL_checktype(L, 2, LUA_TSTRING);
	luaL_checktype(L, 3, LUA_TFUNCTION);

	luaL_getmetatable(L, luaL_checkstring(L, 1));
	luaL_argcheck(L, !lua_isnil(L, -1), 1, "no such metatable");
	lua_getfield(L, -1, "__index");

	lua_pushvalue(L, 2); /* push method name */
	lua_gettable(L, -2); /* push old method */

	lua_pushvalue(L, 2); /* push method name */
	lua_pushvalue(L, 3); /* push new method */
	lua_settable(L, -4); /* replace old method */

	return 1; /* return old method */
} /* opendkim_interpose() */

static int opendkim_band(lua_State *L) {
	lua_pushinteger(L, luaL_checkinteger(L, 1) & luaL_checkinteger(L, 2));

	return 1;
} /* opendkim_band() */

static int opendkim_bnot(lua_State *L) {
	lua_pushinteger(L, ~luaL_checkinteger(L, 1));

	return 1;
} /* opendkim_bnot() */

static int opendkim_bor(lua_State *L) {
	lua_pushinteger(L, luaL_checkinteger(L, 1) | luaL_checkinteger(L, 2));

	return 1;
} /* opendkim_bor() */

static int opendkim_btest(lua_State *L) {
	lua_Integer t = ~0;

	do {
		t &= luaL_checkinteger(L, -1);
		lua_pop(L, 1);
	} while (lua_gettop(L) > 0);

	lua_pushboolean(L, t != 0);

	return 1;
} /* opendkim_btest() */

static luaL_Reg opendkim_globals[] = {
	{ "init", opendkim_init },
	{ "libversion", opendkim_libversion },
	{ "ssl_version", opendkim_ssl_version },
	{ "getresultstr", opendkim_getresultstr },
	{ "sig_geterrorstr", opendkim_sig_geterrorstr },
	{ "mail_parse", opendkim_mail_parse },
#if 0 /* not declared in dkim.h */
	{ "mail_parse_multi", opendkim_mail_parse_multi },
#endif
	{ "interpose", opendkim_interpose },
	{ "band", opendkim_band },
	{ "bnot", opendkim_bnot },
	{ "bor", opendkim_bor },
	{ "btest", opendkim_btest },
	{ NULL, NULL },
}; /* opendkim_globals[] */

static struct {
	const char name[32];
	lua_Integer value;
} opendkim_const[] = {
#include "opendkim-const.h"
};

int luaopen_opendkim_core(lua_State *L) {
	size_t i;

	auxL_newmetatable(L, "DKIM_LIB*", DKIM_LIB_methods, DKIM_LIB_metamethods, 0);
	lua_pop(L, 1);

	auxL_newmetatable(L, "DKIM*", DKIM_methods, DKIM_metamethods, 0);
	lua_pop(L, 1);

	auxL_newmetatable(L, "DKIM_SIGINFO*", DKIM_SIGINFO_methods, DKIM_SIGINFO_metamethods, 0);
	lua_pop(L, 1);

	auxL_newmetatable(L, "DKIM_QUERYINFO*", DKIM_QUERYINFO_methods, DKIM_QUERYINFO_metamethods, 0);
	lua_pop(L, 1);

	luaL_newlib(L, opendkim_globals);

	for (i = 0; i < sizeof opendkim_const / sizeof *opendkim_const; i++) {
		lua_pushstring(L, opendkim_const[i].name);
		lua_pushinteger(L, opendkim_const[i].value);
		lua_settable(L, -3);
	}

	return 1;
} /* luaopen_opendkim_core() */
