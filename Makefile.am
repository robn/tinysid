## Process this file with automake to produce Makefile.in 

SUBDIRS = src

# Requires automake 1.5
AUTOMAKE_OPTIONS = 1.5 foreign

# Add spec file, PSID demo files and Frodo sources to distribution
dist-hook:
	cp SIDPlayer.spec $(distdir)
	cp -R $(srcdir)/"PSID Demo" $(distdir)
	rm -rf $(distdir)/"PSID Demo"/CVS
	for i in cpu_macros.h cpu_opcodes.h prefs.h sid.cpp; do \
	  if [ -L $(distdir)/src/$$i ]; then \
	    rm -f $(distdir)/src/$$i; \
	    cp -p $(srcdir)/../Frodo/src/$$i $(distdir)/src; \
	  fi; \
	done

# Rule to build tar-gzipped distribution package
$(PACKAGE)-$(VERSION).tar.gz: dist

# Rule to build RPM distribution package
rpm: $(PACKAGE)-$(VERSION).tar.gz
	rpmbuild -ta --clean $(PACKAGE)-$(VERSION).tar.gz
