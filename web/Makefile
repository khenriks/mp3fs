# Generate web page

.PHONY: all clean
all: index.html

clean:
	rm -f index.html

index.html: ../README.rst
	@echo "  GEN   " $@
	@rst2html.py --stylesheet=voidspace.css --link-stylesheet $< $@
