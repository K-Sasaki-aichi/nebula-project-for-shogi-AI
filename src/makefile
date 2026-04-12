CXX = clang++
CXXFLAGS = -std=c++2b -I./nshogi/src
LDFLAGS = -L./nshogi/build/release_clang++/lib -lnshogi_static

TARGET = test-app
SRC = engine/main.cpp

all:
	$(CXX) $(SRC) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)