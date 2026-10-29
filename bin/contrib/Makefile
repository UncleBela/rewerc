CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic

SOURCES = md2html.cpp metadata.cpp callouts.cpp footnotes.cpp toc.cpp published-files.cpp
HEADERS = metadata.h callouts.h footnotes.h toc.h published-files.h

all: md2html werc-guard

md2html: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@.new $(SOURCES) $(LDLIBS)
	mv -f $@.new $@

werc-guard: werc-guard.cpp published-files.cpp published-files.h auth.cpp auth.h utf8.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@.new werc-guard.cpp published-files.cpp auth.cpp $(LDLIBS) -lcrypto -lsqlite3
	mv -f $@.new $@

.PHONY: all clean
clean:
	rm -f md2html md2html.new werc-guard werc-guard.new
