top_builddir = ../..
include $(top_builddir)/src/Makefile.global

MVN_EXISTS := $(shell command -v mvn 2> /dev/null)

all:
	mvn package -DskipTests

test:
	cd test && ./make-test.sh

clean:
ifdef MVN_EXISTS
	mvn clean
endif

install: all
	$(MKDIR_P) $(pkglibdir)/java/
	$(INSTALL_DATA) 'target/dlagent-1.0.0.jar' $(pkglibdir)/java/

uninstall:
	rm -f $(pkglibdir)/java/dlagent-1.0.0.jar

distclean: clean
