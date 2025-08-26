CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
TARGET = sip
SOURCE = sip.cpp

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
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install