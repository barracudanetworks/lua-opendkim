
$(top_srcdir)/src/5.1/opendkim.so: $(top_srcdir)/src/opendkim.c
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) $(LUA51_CPPFLAGS) $(CPPFLAGS) -DOPENDKIM_LUA_VERSION_NUM=501 -o $@ $^ $(SOFLAGS) $(LDFLAGS) $(LIBS)

$(top_srcdir)/src/5.2/opendkim.so: $(top_srcdir)/src/opendkim.c
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) $(LUA52_CPPFLAGS) $(CPPFLAGS) -DOPENDKIM_LUA_VERSION_NUM=502 -o $@ $^ $(SOFLAGS) $(LDFLAGS) $(LIBS)

$(top_srcdir)/src/5.3/opendkim.so: $(top_srcdir)/src/opendkim.c
	$(MKDIR_P) $(@D)
	$(CC) $(CFLAGS) $(LUA53_CPPFLAGS) $(CPPFLAGS) -DOPENDKIM_LUA_VERSION_NUM=503 -o $@ $^ $(SOFLAGS) $(LDFLAGS) $(LIBS)

$(DESTDIR)$(lua51cpath)/opendkim.so: $(top_srcdir)/src/5.1/opendkim.so
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua52cpath)/opendkim.so: $(top_srcdir)/src/5.2/opendkim.so
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(DESTDIR)$(lua53cpath)/opendkim.so: $(top_srcdir)/src/5.3/opendkim.so
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $^ $@

$(top_srcdir)/src/uninstall:
	for cpath in "$(lua51cpath)" "$(lua52cpath)" "$(lua53cpath)"; do \ 
		[ -z "$${cpath}" ] || $(RM) -f "$(DESTDIR)$${cpath}/opendkim.so"; \
	done

$(top_srcdir)/src/clean:
	$(RM) -fr $(@D)/5.1 $(@D)/5.2 $(@D)/5.3

clean: $(top_srcdir)/src/clean
