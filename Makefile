all: # make default target by specifying first

top_srcdir := $(abspath $(lastword $(MAKEFILE_LIST))/..)

-include $(top_srcdir)/Makeflags

prefix ?= /usr/local
includedir ?= $(prefix)/include
libdir ?= $(prefix)/lib
datadir ?= $(prefix)/share
lua51cpath ?= $(or $(shell $(top_srcdir)/mk/luapath -krm5 -v5.1 package.cpath), $(libdir)/lua/5.1)
lua52cpath ?= $(or $(shell $(top_srcdir)/mk/luapath -krm5 -v5.2 package.cpath), $(libdir)/lua/5.2)
lua53cpath ?= $(or $(shell $(top_srcdir)/mk/luapath -krm5 -v5.3 package.cpath), $(libdir)/lua/5.3)

LUA51_CPPFLAGS ?= $(shell $(top_srcdir)/mk/luapath -krm5 -v5.1 cppflags)
LUA52_CPPFLAGS ?= $(shell $(top_srcdir)/mk/luapath -krm5 -v5.2 cppflags)
LUA53_CPPFLAGS ?= $(shell $(top_srcdir)/mk/luapath -krm5 -v5.3 cppflags)

RM ?= rm
MKDIR ?= mkdir
MKDIR_P ?= $(MKDIR) -p
INSTALL ?= install
INSTALL_DATA ?= $(INSTALL) -m644

$(top_srcdir)/Makeflags.tmp:
	printf 'prefix := $(value prefix)'"\n" >| $@
	printf 'includedir := $(value includedir)'"\n" >> $@
	printf 'libdir := $(value libdir)'"\n" >> $@
	printf 'datadir := $(value datadir)'"\n" >> $@
	printf 'lua51cpath := $(lua51cpath)'"\n" >> $@
	printf 'lua52cpath := $(lua52cpath)'"\n" >> $@
	printf 'lua53cpath := $(lua53cpath)'"\n" >> $@
	printf 'CPPFLAGS := $(value CPPFLAGS)'"\n" >> $@
	printf 'CFLAGS := $(value CFLAGS)'"\n" >> $@
	printf 'SOFLAGS := $(value SOFLAGS)'"\n" >> $@
	printf 'LDFLAGS := $(value LDFLAGS)'"\n" >> $@
	printf 'LIBS := $(value LIBS)'"\n" >> $@
	printf 'LUA51_CPPFLAGS := $(LUA51_CPPFLAGS)'"\n" >> $@
	printf 'LUA52_CPPFLAGS := $(LUA52_CPPFLAGS)'"\n" >> $@
	printf 'LUA53_CPPFLAGS := $(LUA53_CPPFLAGS)'"\n" >> $@
	if [ -n "$$($(@D)/mk/luapath -krm5 -v5.1 version)" ]; then \
		printf 'all: $$(top_srcdir)/src/5.1/opendkim.so'"\n" >> $@; \
		printf 'install: $$(lua51cpath)/opendkim.so'"\n" >> $@; \
	fi
	if [ -n "$$($(@D)/mk/luapath -krm5 -v5.2 version)" ]; then \
		printf 'all: $$(top_srcdir)/src/5.2/opendkim.so'"\n" >> $@; \
		printf 'install: $$(lua52cpath)/opendkim.so'"\n" >> $@; \
	fi
	if [ -n "$$($(@D)/mk/luapath -krm5 -v5.3 version)" ]; then \
		printf 'all: $$(top_srcdir)/src/5.3/opendkim.so'"\n" >> $@; \
		printf 'install: $$(lua53cpath)/opendkim.so'"\n" >> $@; \
	fi

.PHONY: config configure

config configure: $(top_srcdir)/Makeflags.tmp
	F="$^"; mv "$${F}" "$${F%.tmp}"

include $(top_srcdir)/src/Rules.mk

distclean:
	$(RM) -f $(top_srcdir)/Makeflags
