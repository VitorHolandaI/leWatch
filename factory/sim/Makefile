# PC simulator (SDL2 + LVGL) for the T-Watch S3 UI.
# Builds the portable UI code with ARDUINO undefined (hardware -> PC stubs).

ROOT  := ..
LVGL  := ../../libraries/lvgl

CC    := gcc
CXX   := g++

SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

INC := -I$(ROOT) -I$(LVGL) -I$(LVGL)/src -I. $(SDL_CFLAGS)
DEF := -DLV_CONF_INCLUDE_SIMPLE -DSDL_HOR_RES=240 -DSDL_VER_RES=240 -DARDUINO_T_WATCH_S3 -DUSING_IR_REMOTE
CFLAGS   := $(INC) $(DEF) -O0 -g -w -include compat.h
CXXFLAGS := $(CFLAGS) -std=c++17
QUALITY_WARN := -Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Wformat=2 -Wundef
QUALITY_CFLAGS := $(filter-out -w,$(CFLAGS)) $(QUALITY_WARN)
QUALITY_CXXFLAGS := $(QUALITY_CFLAGS) -std=c++17

C_SRC   := $(shell find $(LVGL)/src -name '*.c') $(shell find $(ROOT)/src -name '*.c')
CXX_SRC := $(ROOT)/main.cpp $(ROOT)/hal_interface.cpp $(ROOT)/app_nfc.cpp stubs.cpp $(wildcard $(ROOT)/ui_*.cpp)
CPPCHECK_SRC := *.cpp sim/stubs.cpp
CPPCHECK_DEF := -DARDUINO_T_WATCH_S3 -DUSING_IR_REMOTE -DESP_IDF_VERSION=0 '-DESP_IDF_VERSION_VAL(a,b,c)=0'
LIZARD_OPTS := -l cpp -C 15 -L 80 -x './src/*' -x './audio/*'

OBJ := $(C_SRC:.c=.o) $(CXX_SRC:.cpp=.o)

sim: $(OBJ)
	$(CXX) $(OBJ) -o sim $(SDL_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	find $(LVGL)/src -name '*.o' -delete 2>/dev/null
	find $(ROOT)/src -name '*.o' -delete 2>/dev/null
	rm -f $(ROOT)/*.o sim

quality-build: clean
	$(MAKE) CFLAGS="$(QUALITY_CFLAGS)" CXXFLAGS="$(QUALITY_CXXFLAGS)" sim

cppcheck:
	cd $(ROOT) && cppcheck --enable=warning,style,performance,portability --std=c++17 --inline-suppr --suppress=missingIncludeSystem --suppress=ctuOneDefinitionRuleViolation $(CPPCHECK_DEF) $(CPPCHECK_SRC)

lizard:
	cd $(ROOT) && uv run lizard $(LIZARD_OPTS) -i 999 .

lizard-strict:
	cd $(ROOT) && uv run lizard $(LIZARD_OPTS) .

clang-tidy-db: clean
	cd $(ROOT) && bear -- make -C sim

smoke: sim
	SDL_VIDEODRIVER=dummy timeout 10 ./sim; code=$$?; test $$code -eq 0 -o $$code -eq 124

quality: quality-build cppcheck lizard smoke

.PHONY: clean quality-build cppcheck lizard lizard-strict clang-tidy-db smoke quality
