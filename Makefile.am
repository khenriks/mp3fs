SUBDIRS = src

# Remove absolutely every generated file
.PHONY: squeaky-clean
squeaky-clean: maintainer-clean
	rm -rf aclocal.m4 configure config Makefile.in src/Makefile.in

.PHONY: website
website:
	@echo "Making all in web"
	@$(MAKE) -C web all

clean-local: clean-local-web

.PHONY: clean-local-web
clean-local-web:
	@test -d web \
		&& (echo "Making clean in web" ; $(MAKE) -C web clean) \
		|| true
