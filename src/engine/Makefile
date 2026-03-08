# --- src/engine/Makefile (MSYS2用) ---
CXX      = g++
CXXFLAGS = -O3 -std=c++20 -DNDEBUG -w

INCLUDES = -I../nshogi/src -I../nshogi

NSHOGI_DIR = ../nshogi/src
NSHOGI_SRCS = $(NSHOGI_DIR)/buildinfo/capability.cc \
              $(NSHOGI_DIR)/core/initializer.cc \
              $(NSHOGI_DIR)/core/position.cc \
              $(NSHOGI_DIR)/core/positionbuilder.cc \
              $(NSHOGI_DIR)/core/state.cc \
              $(NSHOGI_DIR)/core/statebuilder.cc \
              $(NSHOGI_DIR)/core/movegenerator.cc \
              $(NSHOGI_DIR)/core/huffman.cc \
              $(NSHOGI_DIR)/core/internal/bitboard.cc \
              $(NSHOGI_DIR)/core/internal/huffmanimpl.cc \
              $(NSHOGI_DIR)/core/internal/hash.cc \
              $(NSHOGI_DIR)/core/internal/movegenerator.cc \
              $(NSHOGI_DIR)/core/internal/statehelper.cc \
              $(NSHOGI_DIR)/core/internal/stateimpl.cc \
              $(NSHOGI_DIR)/io/huffman.cc \
              $(NSHOGI_DIR)/io/sfen.cc \
              $(NSHOGI_DIR)/io/csa.cc \
              $(NSHOGI_DIR)/io/file.cc

TARGET = engine_app.exe

run: $(TARGET)
	./$(TARGET)

$(TARGET): main.cpp $(NSHOGI_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp $(NSHOGI_SRCS) -o $(TARGET)

clean:
    rm -f $(TARGET)