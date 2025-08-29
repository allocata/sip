CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra

# Static linking for Windows to avoid DLL dependencies
ifeq ($(OS),Windows_NT)
	CXXFLAGS += -static-libgcc -static-libstdc++
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
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install