CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra

PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin

# Static linking for Windows to avoid DLL dependencies
ifeq ($(OS),Windows_NT)
	CXXFLAGS += -static-libgcc -static-libstdc++ -static
	LDFLAGS += -static
	TARGET = sip.exe
	RM = del
else
	TARGET = sip
	RM = rm -f
endif

SOURCE = sip.cpp

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

clean:
	$(RM) $(TARGET)
ifeq ($(OS),Windows_NT)
	-$(RM) sip.exe 2>nul
endif

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(TARGET) $(DESTDIR)$(BINDIR)/

.PHONY: all clean install