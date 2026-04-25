CXX = clang++
# -mavx2 を必ず追加する。さらに最適化の -O3 も入れるのが一般的。
#CXXFLAGS = -std=c++2b -I./nshogi/src -mavx2 -O3
CXXFLAGS = -std=c++2b -I./nshogi/src -mtune=native -march=native -O3

LDFLAGS = -L./nshogi/build/release_clang++/lib -lnshogi_static

TARGET = nn_test3

# SRCの指定。 engine/*.cpp だけでなく engine/nn/ などのサブディレクトリも含める。
# もしこれでも足りなければ、手動で path/to/file.cpp を並べてください。
SRC = $(wildcard engine/*.cpp) \
      $(wildcard engine/nn/*.cpp) \
      $(wildcard model/*.cpp)

all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)