
SHELL:=bash
BINNAME:=horsec

ifndef DEBUGGABLE
override DEBUGGABLE = true
endif

# -------- DEPENDENCY PATHS, to be located from ./vendor --------
PHYSFSPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.lower().startswith("physfs-") or f.lower() == "physfs":\n    print("vendor/" + f)\n' | python3)
OPENSSLPATH=$(shell pwd)/$(shell echo -e 'import os\nfor f in os.listdir("./vendor"):\n  if not os.path.isdir("vendor/" + f):\n    continue\n  if f.lower().startswith("openssl-") or f.lower() == "openssl":\n    print("vendor/" + f)\n' | python3)

# -------- CFLAGS & LDFLAGS DEFAULTS --------
CANUSESSE=`python3 tools/can-use-sse.py`
ifeq ($(CANUSESSE),true)
SSEFLAG:=-msse2 -march=haswell
else
SSEFLAG:=
endif
ifeq ($(DEBUGGABLE),true)
ifneq (,$(findstring mingw,$(CC)))
CFLAGS_OPTIMIZATION:=-O0 -gstabs $(SSEFLAG) -fno-omit-frame-pointer
else
CFLAGS_OPTIMIZATION:=-O0 -g $(SSEFLAG) -fno-omit-frame-pointer
endif
else
CFLAGS_OPTIMIZATION:=-Ofast -s $(SSEFLAG) -fno-associative-math -fno-finite-math-only -fomit-frame-pointer -DNDEBUG
endif
OPENSSLHOSTOPTION:=linux-generic64
CXXFLAGS:=-fexceptions
CFLAGS:= -DBUILD_TIME=\"`date -u +'%Y-%m-%dT%H:%M:%S'`\" -Wall -Wextra -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable $(CFLAGS_OPTIMIZATION) -I. -Ihorse64/ -I"vendor/" -I"$(PHYSFSPATH)/src/" -L"$(PHYSFSPATH)" -I"$(OPENSSLPATH)/include/" -L"$(OPENSSLPATH)" -Wl,-Bdynamic
LDFLAGS:= -Wl,-Bstatic -lphysfs -lh64openssl -lh64crypto -Wl,-Bdynamic
TEST_OBJECTS:=$(patsubst %.c, %.o, $(wildcard ./horse64/test_*.c) $(wildcard ./horse64/compiler/test_*.c))
ALL_OBJECTS:=$(filter-out ./horse64/vmexec_inst_unopbinop_INCLUDE.o, $(patsubst %.c, %.o, $(wildcard ./horse64/*.c) $(wildcard ./horse64/corelib/*.c) $(wildcard ./horse64/compiler/*.c)) vendor/siphash.o)
TEST_BINARIES:=$(patsubst %.o, %.bin, $(TEST_OBJECTS))
PROGRAM_OBJECTS:=$(filter-out $(TEST_OBJECTS),$(ALL_OBJECTS))
PROGRAM_OBJECTS_NO_MAIN:=$(filter-out ./horse64/main.o,$(PROGRAM_OBJECTS))
BINEXT:=

# -------- PLATFORM-SPECIFIC FLAGS --------
ifneq (,$(findstring mingw,$(CC)))
CFLAGS+= -mthreads -static-libgcc -static-libstdc++ -mwindows
BINEXT:=.exe
PLATFORM:=windows
CROSSCOMPILEHOST:=$(shell echo -e 'print("'$(CC)'".rpartition("-")[0])' | python3)
OPENSSLHOSTOPTION:=mingw64 --cross-compile-prefix=$(CROSSCOMPILEHOST)-
HOSTOPTION:= --host=$(CROSSCOMPILEHOST)
LDFLAGS+= -lwininet -lws2_32 -lole32 -lgdi32 -lshell32 -lwinmm -luser32 -luuid -lodbc32 -loleaut32 -limm32 -lhid -lversion -lsetupapi -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
STRIPTOOL:=$(CROSSCOMPILEHOST)-strip
CXX:=$(CROSSCOMPILEHOST)-g++
BULLETCXX:=$(CXX)
else
CFLAGS+= -pthread
PLATFORM:=linux
HOSTOPTION:=
LDFLAGS+= -lm -ldl
STRIPTOOL:=strip
ifneq (,$(findstring aarch64,$(CC)))
CROSSCOMPILEHOST:=$(shell echo -e 'print("'$(CC)'".rpartition("-")[0])' | python3)
CXX:=$(CROSSCOMPILEHOST)-g++
OPENSSLHOSTOPTION:=linux-generic64 --cross-compile-prefix=$(CROSSCOMPILEHOST)-
STRIPTOOL:=$(CROSSCOMPILEHOST)-strip
endif
endif

.PHONY: test remove-main-o check-submodules datapak release debug wchar_data openssl

debug: all
showvariables:
	@echo "CC: $(CC)"
	@echo "CXX: $(CXX)"
	@echo "BULLETCXX: $(BULLETCXX)"
	@echo "All objects: $(ALL_OBJECTS)"
	@echo "Test objects: $(TEST_OBJECTS)"
	@echo "Program objects: $(PROGRAM_OBJECTS)"
	@echo "Cross-compile host: $(CROSSCOMPILEHOST)"
all: wchar_data remove-main-o check-submodules datapak $(PROGRAM_OBJECTS)
	$(CXX) $(CFLAGS) -o ./"$(BINNAME)$(BINEXT)" $(PROGRAM_OBJECTS) $(LDFLAGS)
ifneq ($(DEBUGGABLE),true)
	$(STRIPTOOL) ./"$(BINNAME)$(BINEXT)"
endif
wchar_data:
	tools/generate-unicode-headers.py
remove-main-o:
	rm -f horse64/main.o
%.o: %.c $.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.oxx: %.cpp
	$(CXX) $(CXXFLAGS) $(CFLAGS) -c -o $@ $<

checkdco:
	python3 tools/check-dco.py
test: checkdco $(ALL_OBJECTS) $(TEST_BINARIES)
	for x in $(TEST_BINARIES); do echo ">>> TEST RUN: $$x"; CK_FORK=no valgrind --track-origins=yes --leak-check=full ./$$x || { exit 1; }; done
	@echo "All tests were run."
test_%.bin: test_%.c $(PROGRAM_OBJECTS_NO_MAIN)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -pthread -o ./$(basename $@).bin $(basename $<).o $(PROGRAM_OBJECTS_NO_MAIN) -lcheck -lrt -lsubunit $(LDFLAGS)

check-submodules:
	@if [ ! -e "$(PHYSFSPATH)/README.txt" ]; then echo ""; echo -e '\033[0;31m$$(PHYSFSPATH)/README.txt missing. Did you download the submodules?\033[0m'; echo "Try this:"; echo ""; echo "    git submodule init && git submodule update"; echo ""; exit 1; fi
	@echo "Submodules appear to exist."
	@if [ ! -e "$(PHYSFSPATH)/libphysfs.a" ]; then echo "Warning, dependencies appear to be missing. Automatically running build-deps target."; make build-deps; fi
	@echo "Submodules appear to have been built some time. (Run build-deps to build them again.)"

datapak:
	rm -rf ./datapak/
	mkdir -p ./datapak/
	cp -R ./horse_modules_builtin/ ./datapak/horse_modules_builtin/
	cd datapak && find . -name "*~" -type f -delete
	cd datapak && find . -name "*.swp" -type f -delete
	cd datapak && zip -r -9 ./coreapi.h64pak ./horse_modules_builtin/
	mv -f datapak/coreapi.h64pak ./coreapi.h64pak
	rm -rf datapak
release:
	make DEBUGGABLE=false CC="$(CC)" CXX="$(CXX)"
build-deps:
	make physfs openssl DEBUGGABLE="$(DEBUGGABLE)" CC="$(CC)" CXX="$(CXX)"

clean:
	rm -f $(ALL_OBJECTS) coreapi.h3dpak $(TEST_BINARIES)

physfs:
	CC="$(CC)" python3 tools/physfsmakefile.py > $(PHYSFSPATH)/Makefile
	cd $(PHYSFSPATH) && rm -f libphysfs.a && make clean && make CC="$(CC)" CXX="$(CXX)"

openssl:
	cd $(OPENSSLPATH) && rm -f lib*.a && ./Configure $(OPENSSLHOSTOPTION) no-engine no-comp no-hw no-shared threads CC="$(CC)" && make clean && make CC="$(CC)" CXX="$(CXX)" && cp libssl.a libh64openssl.a && cp libcrypto.a libh64crypto.a

veryclean: clean
	rm -f $(BINNAME)-*.exe $(BINNAME)-*.bin
	cd $(OPENSSLPATH) && rm -f lib*.a
	cd $(PHYSFSPATH) && rm -f libphysfs.a

install:
	echo "Use horp to install horsec. Or just run the horsec binary directly."
	exit 0
