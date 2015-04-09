#!/bin/sh
set -e # strict error
set -f # disable pathname expansion
set -C # noclobber
unset IFS

SRCDIR="$(cd "${0%/*}/.." && pwd -L)"
PATH="${PATH:-$(command -p getconf PATH)}:${SRCDIR}/mk"

lua51path="${SRCDIR}/src/5.1"
lua51cpath="${SRCDIR}/src/5.1"
lua52path="${SRCDIR}/src/5.2"
lua52cpath="${SRCDIR}/src/5.2"
lua53path="${SRCDIR}/src/5.3"
lua53cpath="${SRCDIR}/src/5.3"

export LUA_PATH="${lua51path}/?.lua;${SRCDIR}/regress/?.lua;;"
export LUA_CPATH="${lua51cpath}/?.so;;"
export LUA_PATH_5_2="${lua52path}/?.lua;${SRCDIR}/regress/?.lua;;"
export LUA_CPATH_5_2="${lua52cpath}/?.so;;"
export LUA_PATH_5_3="${lua53path}/?.lua;${SRCDIR}/regress/?.lua;;"
export LUA_CPATH_5_3="${lua53cpath}/?.so;;"

(cd "${SRCDIR}" && make -s all)

export PROGNAME="${0##*/}"
