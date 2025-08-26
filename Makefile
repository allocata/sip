CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
TARGET = sip
SOURCE = sip.cpp

PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin

ifeq ($(OS),Windows_NT)
	TARGET = sip.exe
	RM = del
else
	RM = rm -f
endif

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