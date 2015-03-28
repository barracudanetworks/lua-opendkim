
$(top_srcdir)/src/5.1/opendkim.lua: $(top_srcdir)/src/opendkim.lua
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(top_srcdir)/src/5.2/opendkim.lua: $(top_srcdir)/src/opendkim.lua
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(top_srcdir)/src/5.3/opendkim.lua: $(top_srcdir)/src/opendkim.lua
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(top_srcdir)/src/5.1/opendkim/core.so: $(top_srcdir)/src/opendkim.c
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) $(LUA51_CPPFLAGS) $(CPPFLAGS) -DOPENDKIM_LUA_VERSION_NUM=501 -o $@ $^ $(SOFLAGS) $(LDFLAGS) $(LIBS)

$(top_srcdir)/src/5.2/opendkim/core.so: $(top_srcdir)/src/opendkim.c
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) $(LUA52_CPPFLAGS) $(CPPFLAGS) -DOPENDKIM_LUA_VERSION_NUM=502 -o $@ $^ $(SOFLAGS) $(LDFLAGS) $(LIBS)

$(top_srcdir)/src/5.3/opendkim/core.so: $(top_srcdir)/src/opendkim.c
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) $(LUA53_CPPFLAGS) $(CPPFLAGS) -DOPENDKIM_LUA_VERSION_NUM=503 -o $@ $^ $(SOFLAGS) $(LDFLAGS) $(LIBS)

$(DESTDIR)$(lua51path)/opendkim.lua: $(top_srcdir)/src/5.1/opendkim.lua
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua52path)/opendkim.lua: $(top_srcdir)/src/5.2/opendkim.lua
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua53path)/opendkim.lua: $(top_srcdir)/src/5.3/opendkim.lua
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua51cpath)/opendkim/core.so: $(top_srcdir)/src/5.1/opendkim/core.so
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua52cpath)/opendkim/core.so: $(top_srcdir)/src/5.2/opendkim/core.so
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua53cpath)/opendkim/core.so: $(top_srcdir)/src/5.3/opendkim/core.so
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(top_srcdir)/src/uninstall:
	for path in "$(lua51path)" "$(lua52path)" "$(lua53path)"; do \
		[ -z "$${path}" ] || $(RM) -f "$(DESTDIR)$${path}/opendkim.lua"; \
	done
	for cpath in "$(lua51cpath)" "$(lua52cpath)" "$(lua53cpath)"; do \
		[ -n "$${cpath}" ] || continue; \
		$(RM) -f "$(DESTDIR)$${cpath}/opendkim/core.so"; \
		[ ! -d "$(DESTDIR)$${cpath}/opendkim" ] || $(RMDIR) "$(DESTDIR)$${cpath}/opendkim" || true; \
	done

uninstall: $(top_srcdir)/src/uninstall

$(top_srcdir)/src/clean:
	$(RM) -fr $(@D)/5.1 $(@D)/5.2 $(@D)/5.3

clean: $(top_srcdir)/src/clean
