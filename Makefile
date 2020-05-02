
SHELL:=bash
BINNAME:=horsec

ifndef DEBUGGABLE
override DEBUGGABLE = true
endif

# -------- DEPENDENCY PATHS, to be located from ./vendor --------
LUAPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.startswith("lua-"):\n    print("vendor/" + f)\n' | python3)
PHYSFSPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.lower().startswith("physfs-") or f.lower() == "physfs":\n    print("vendor/" + f)\n' | python3)
FREETYPEPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.lower().startswith("freetype"):\n    print("vendor/" + f)\n' | python3)
SDLPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if (f.lower().startswith("sdl-") or f.lower() == "sdl") and "ttf" not in f.lower():\n    print("vendor/" + f)\n' | python3)
SDLTTFPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.lower().replace("-", "").replace("_", "").startswith("sdlttf") or f.lower().replace("-", "").replace("_", "").startswith("sdl2ttf"):\n    print("vendor/" + f)\n' | python3)
BULLETPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.startswith("bullet3-") or f == "bullet3":\n    print("vendor/" + f)\n' | python3)
TOOLRENAMEBULLET=$(shell pwd)/tools/rename-bullet-results.py

# -------- CFLAGS & LDFLAGS DEFAULTS --------
ifeq ($(DEBUGGABLE),true)
CFLAGS_OPTIMIZATION:=-O0 -g -msse2 -fno-omit-frame-pointer
else
CFLAGS_OPTIMIZATION:=-Ofast -s -msse2 -fomit-frame-pointer -DNDEBUG
endif
CXXFLAGS:=-fexceptions
CFLAGS:= -Wall -Wextra -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable -DMATHC_USE_DOUBLE_FLOATING_POINT -DMATHC_USE_FLOATING_POINT $(CFLAGS_OPTIMIZATION) -I. -Ihorse64/ -I"vendor/" -L"$(LUAPATH)/" -I"$(LUAPATH)/" -L"$(FREETYPEPATH)/objs/.libs/" -L"$(FREETYPEPATH)/objs/" -I"$(SDLTTFPATH)/" -L"$(SDLTTFPATH)/.libs/" -L"$(BULLETPATH)/bin/" -I"$(BULLETPATH)/src" -I"$(SDLPATH)/include/" -I"$(PHYSFSPATH)/src/" -I"./vendor/khronos-opengles2-apis" -L"$(PHYSFSPATH)" -L"$(SDLPATH)/build/.libs/"  -Wl,-Bdynamic
LDFLAGS:= -Wl,-Bstatic -lphysfs -lBulletDynamics -lBulletCollision -lLinearMath -llua -lSDL2 -lSDL2_ttf -lfreetype -Wl,-Bdynamic
TEST_OBJECTS:=$(patsubst %.c, %.o, $(wildcard ./horse64/test_*.c) $(wildcard ./horse64/compiler/test_*.c))
ALL_OBJECTS:=$(patsubst %.c, %.o, $(wildcard ./horse64/*.c) $(wildcard ./horse64/compiler/*.c) $(wildcard ./horse64/collision/*.c)) $(patsubst %.cpp, %.oxx, $(wildcard ./horse64/collision/*.cpp)) vendor/mathc/mathc.o vendor/siphash.o
TEST_BINARIES:=$(patsubst %.o, %.bin, $(TEST_OBJECTS))
PROGRAM_OBJECTS:=$(filter-out $(TEST_OBJECTS),$(ALL_OBJECTS))
PROGRAM_OBJECTS_NO_MAIN:=$(filter-out ./horse64/main.o,$(PROGRAM_OBJECTS))
BINEXT:=

# -------- PLATFORM-SPECIFIC FLAGS --------
ifneq (,$(findstring mingw,$(CC)))
CFLAGS+= -mthreads -static-libgcc -static-libstdc++ -mwindows -DHORSE3D_DESKTOPGL
BINEXT:=.exe
PLATFORM:=windows
CROSSCOMPILEHOST:=$(shell echo -e 'print("'$(CC)'".rpartition("-")[0])' | python3)
HOSTOPTION:= --host=$(CROSSCOMPILEHOST)
CROSSCOMPILESYSROOT:=$(shell dirname $(shell dirname $(shell locate $(CROSSCOMPILEHOST) | grep lib/pkgconfig)))
LDFLAGS+= -lwininet -lole32 -lgdi32 -lshell32 -lwinmm -luser32 -luuid -lodbc32 -loleaut32 -limm32 -lhid -lversion -lsetupapi -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
STRIPTOOL:=$(shell echo -e 'print("'$(CC)'".rpartition("-")[0])' | python3)-strip
ifeq (,$(findstring mingw,$(CXX)))
CXX:=$(CROSSCOMPILEHOST)-g++
endif
else
CFLAGS+= -pthread
PLATFORM:=linux
HOSTOPTION:=
LDFLAGS+= -lm -ldl
STRIPTOOL:=strip
endif

.PHONY: test check-submodules sdl2 sdlttf freetype datapak release debug bullet3 lua releases

debug: all
testo:
	echo "All objects: $(ALL_OBJECTS)"
	echo "Test objects: $(TEST_OBJECTS)"
	echo "Program objects: $(PROGRAM_OBJECTS)"
all: check-submodules datapak $(PROGRAM_OBJECTS)
	$(CXX) $(CFLAGS) -o ./"$(BINNAME)$(BINEXT)" $(PROGRAM_OBJECTS) $(LDFLAGS)
ifneq ($(DEBUGGABLE),true)
	$(STRIPTOOL) ./"$(BINNAME)$(BINEXT)"
endif
%.o: %.c $.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.oxx: %.cpp
	$(CXX) $(CXXFLAGS) $(CFLAGS) -c -o $@ $<

test: $(ALL_OBJECTS) $(TEST_BINARIES)
	for x in $(TEST_BINARIES); do echo "TEST: $$x"; ./$$x || { exit 1; }; done
	@echo "All tests were run."
test_%.bin: test_%.c $(PROGRAM_OBJECTS_NO_MAIN)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -pthread -o ./$(basename $@).bin $(basename $<).o $(PROGRAM_OBJECTS_NO_MAIN) -lcheck -lrt -lsubunit $(LDFLAGS)

check-submodules:
	@if [ ! -e "$(SDLPATH)/README.txt" ]; then echo ""; echo -e '\033[0;31m$$(SDLPATH)/README.txt missing. Did you download the submodules?\033[0m'; echo "Try this:"; echo ""; echo "    git submodule init && git submodule update"; echo ""; exit 1; fi
	@echo "Submodules appear to exist."
	@if [ ! -e "$(SDLPATH)/build/.libs/libSDL2.a" ]; then echo "Warning, dependencies appar to be missing. Automatically running build-deps target."; make build-deps; fi
	@echo "Submodules appear to have been built some time. (Run build-deps to build them again.)"

datapak:
	rm -rf ./datapak/
	mkdir -p ./datapak/horse64/
	cp -R ./horse64/scriptcore/ ./datapak/horse64/scriptcore/
	mkdir -p ./demo-assets/
	cp -R ./demo-assets/ ./datapak/demo-assets/
	cp -R ./vendor/packaged-fonts/ ./datapak/demo-assets/packaged-fonts/
	cd datapak && find . -name "*~" -type f -delete
	cd datapak && find . -name "*.swp" -type f -delete
	cd datapak && zip -r -9 ./coreapi.h3dpak ./horse64/ ./demo-assets/
	mv datapak/coreapi.h3dpak ./coreapi.h3dpak
	rm -rf datapak
release:
	make clean
	make build-deps DEBUGGABLE=false CC="$(CC)" CXX="$(CXX)"
	make DEBUGGABLE=false CC="$(CC)" CXX="$(CXX)"
build-deps:
	if [ ! -L "$(SDLPATH)/include/SDL2" ]; then ln -s ../include "$(SDLPATH)/include/SDL2"; fi
	make bullet3 lua sdl2 physfs sdlttf freetype DEBUGGABLE="$(DEBUGGABLE)" CC="$(CC)" CXX="$(CXX)"

bullet3:
	echo "Compiling bullet physics at $(BULLETPATH)"
	rm -rf "$(BULLETPATH)/build3/gmake/"
	rm -f "$(BULLETPATH)/bin/"*
	-cd "$(BULLETPATH)" && make clean
ifeq ($(PLATFORM),linux)
	cd "$(BULLETPATH)"/build3/ && ./premake4_linux64 --double --clamp-velocities gmake && cd gmake && make clean && make config=release64 BulletCollision LinearMath BulletDynamics
else
ifeq ($(PLATFORM),windows)
	cd "$(BULLETPATH)"/build3/ && wine premake4.exe --double --clamp-velocities gmake && cd gmake && make clean && make config=release64 BulletCollision LinearMath BulletDynamics
endif
endif
	cd "$(BULLETPATH)/bin" && python3 $(TOOLRENAMEBULLET)

lua:
	echo "Compiling lua at $(LUAPATH)"
ifeq ($(PLATFORM),linux)
	cd "$(LUAPATH)" && rm -f *.o && make MYLIBS='-ldl -lreadline' MYCFLAGS='-DLUA_USE_LINUX'
else
ifeq ($(PLATFORM),windows)
	cd "$(LUAPATH)" && rm -f *.o && make MYLIBS='' MYCFLAGS='-l,--export-all-symbols'
endif
endif

sdlttf:
	-cd "$(SDLTTFPATH)" && make clean
	sed -i 's/noinst_PROGRAMS =.*//g' "$(SDLTTFPATH)/Makefile.am"
	rm -rf "$(SDLTTFPATH)/.libs/"
ifeq ($(PLATFORM),linux)
	cd "$(SDLTTFPATH)" && bash ./autogen.sh && AUTOMAKE_OPTIONS=foreign autoreconf -f -i; T2_CFLAGS="-I\"$(FREETYPEPATH)/include/\"" SDL_CFLAGS="-I\"$(SDLPATH)/include/\"" ./configure --disable-sdlframework --disable-sdltest --enable-static --disable-shared --disable-freetypetest && make clean && make
else
ifeq ($(PLATFORM),windows)
	cd "$(SDLTTFPATH)" && bash ./autogen.sh && AUTOMAKE_OPTIONS=foreign autoreconf -f -i; FT2_CFLAGS="-I\"$(FREETYPEPATH)/include/\"" SDL_CFLAGS="-I\"$(SDLPATH)/include/\"" ./configure $(HOSTOPTION) --disable-sdlframework --disable-sdltest --enable-static --disable-shared --disable-freetypetest && make clean && make
endif
endif
	if [ ! -L "$(SDLTTFPATH)/SDL2" ]; then ln -s ./ "$(SDLTTFPATH)/SDL2"; fi

freetype:
	-cd "$(FREETYPEPATH)" && make clean
	rm -f "$(FREETYPEPATH)/config.mk"
ifeq ($(PLATFORM),linux)
	cd "$(FREETYPEPATH)" && bash ./autogen.sh && ./configure --with-zlib=no --with-bzip2=no --with-png=no --with-harfbuzz=no && make
else
ifeq ($(PLATFORM),windows)
	cd "$(FREETYPEPATH)" && bash ./autogen.sh && ./configure $(HOSTOPTION) --with-zlib=no --with-bzip2=no --with-png=no --with-harfbuzz=no && make
endif
endif


releases:
	# Clean out releases:
	if [ -d ./releases ]; then rm -rf ./releases/*; fi
	# Native build:
	make veryclean release
	mkdir -p ./releases/Horse3D-Linux/
	cp ./"$(BINNAME)$(BINEXT)" ./releases/Horse3D-Linux/
	cat ./3RDPARTYCREDITS.md ./LICENSE.md > ./releases/Horse3D-Linux/CREDITS.md
	cp ./coreapi.h3dpak ./releases/Horse3D-Linux/
	# Windows build:
	CC=x86_64-w64-mingw32-gcc make release
	mkdir -p ./releases/Horse3D-Windows/
	cp ./"$(BINNAME).exe" ./releases/Horse3D-Windows/
	cp ./coreapi.h3dpak ./releases/Horse3D-Windows/
	cat ./3RDPARTYCREDITS.md ./LICENSE.md > ./releases/Horse3D-Windows/CREDITS.txt

clean:
	rm -f $(ALL_OBJECTS) coreapi.h3dpak $(TEST_BINARIES)

physfs:
	CC="$(CC)" python3 tools/physfsmakefile.py > $(PHYSFSPATH)/Makefile
	cd $(PHYSFSPATH) && make clean && make CC="$(CC)" CXX="$(CXX)"

sdl2:
	rm -f "$(SDLPATH)/Makefile"
	rm -rf "$(SDLPATH)/gen"
	rm -rf "$(SDLPATH)/build/*"
	cp "$(SDLPATH)/include/SDL_config.h" "$(SDLPATH)/include/SDL_config.h.OLD"
ifeq ($(PLATFORM),linux)
	cd "$(SDLPATH)" && ./configure --disable-video-opengles1 --disable-video-vulkan --enable-sse3 --disable-oss --disable-jack --enable-static --disable-video-wayland --disable-shared --enable-ssemath
else
ifeq ($(PLATFORM),windows)
	cd "$(SDLPATH)" && ./configure $(HOSTOPTION) --disable-wasapi --enable-sse3 --enable-static --disable-shared --enable-ssemath
endif
endif
	cd "$(SDLPATH)" && make clean && make
	cp "$(SDLPATH)/include/SDL_config.h.OLD" "$(SDLPATH)/include/SDL_config.h"

veryclean: clean
	rm -f $(BINNAME)-*.exe $(BINNAME)-*.bin
	cd "$(LUAPATH)" && rm -f *.o lib*.a ./lua
