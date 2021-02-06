
FROM ubuntu:18.04

# Upgrade image in general:
RUN apt update -y && DEBIAN_FRONTEND=noninteractive apt upgrade -y

# Install various tools like git, python3, and more:
RUN apt update -y && DEBIAN_FRONTEND=noninteractive apt install -y git zip gdb bash python3 make sed vim gcc g++ g++-mingw-w64 gcc-mingw-w64 mingw-w64 gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Install needed base system libraries:
RUN DEBIAN_FRONTEND=noninteractive apt update && DEBIAN_FRONTEND=noninteractive apt install -y check automake libtool autotools-dev libreadline-dev cmake

# Install valgrind manually for newer version:
RUN mkdir -p /tmp/valgrind && cd /tmp/valgrind && git clone git://sourceware.org/git/valgrind.git .
ADD ./tools/valgrind_patch /tmp/valgrind/valgrind.patch
RUN cd /tmp/valgrind && patch -p1 < ./valgrind.patch
RUN cd /tmp/valgrind && ./autogen.sh && ./configure
RUN cd /tmp/valgrind && make && make install
RUN valgrind --help

# Add in our project code and make sure submodules are initialized:
ADD ./ /compile-tree/
RUN cd /compile-tree/ && git submodule foreach git reset --hard
RUN cd /compile-tree/ && { make check-submodules || git submodule update --init; }

# Volume to write resulting binaries to:
VOLUME /compile-tree/binaries/

# Command to build release builds, and optionally run tests:
CMD bash -c "cd /compile-tree/ && rm -f ./binaries/* && CC=x86_64-w64-mingw32-gcc make veryclean release && rm -f ./binaries/horsec-x64.exe && mv horsec.exe ./binaries/horsec-x64.exe && CC=aarch64-linux-gnu-gcc make veryclean release && mv horsec ./binaries/horsec-aarch64 && make veryclean && if [ \"x$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** BUILDING DEBUG (for tests) ***\"; make clean debug; echo \" *** RUNNING TESTS (DEBUG)***\"; echo ""; make test; fi && make clean release && mv horsec ./binaries/horsec-x64 && chmod -R 777 /compile-tree/binaries/* && if [ \"x$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** RUNNING TESTS (release) ***\"; echo ""; make test; fi"
