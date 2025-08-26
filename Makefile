CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
TARGET = sip
SOURCE = sip.cpp

ifeq ($(OS),Windows_NT)
    TARGET = sip.exe
    CXXFLAGS += -static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -s
    RM = del
else
    RM = rm -f
endif

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

sip.exe: $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o sip.exe

clean:
	$(RM) $(TARGET)
ifeq ($(OS),Windows_NT)
	-$(RM) sip.exe 2>nul
endif

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install