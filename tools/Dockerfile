
FROM ubuntu:18.04

# Upgrade image in general:
RUN apt update -y && DEBIAN_FRONTEND=noninteractive apt upgrade -y

# Install various tools like git, python3, and more:
RUN apt update -y && DEBIAN_FRONTEND=noninteractive apt install -y git zip gdb bash python3 make sed vim gcc g++ g++-mingw-w64 gcc-mingw-w64 mingw-w64 gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Install needed base system libraries:
RUN DEBIAN_FRONTEND=noninteractive apt update && DEBIAN_FRONTEND=noninteractive apt install -y check automake libtool autotools-dev libreadline-dev cmake

# Install valgrind manually for newer version:
RUN valgrind --help || { echo "No valgrind yet, this is expected."; }
RUN mkdir -p /tmp/valgrind && cd /tmp/valgrind && git clone git://sourceware.org/git/valgrind.git .
RUN cd /tmp/valgrind && git checkout b918f710208889d579026627830a80b24be1513d
ADD ./vendor/valgrind.patch /tmp/valgrind/valgrind.patch
RUN cd /tmp/valgrind && patch -p1 < ./valgrind.patch
RUN cd /tmp/valgrind && ./autogen.sh && ./configure
RUN cd /tmp/valgrind && make && make install
RUN valgrind --help; valgrind --version

# Command to build release builds, and optionally run tests:
RUN echo "#!/bin/bash" > /do-build
RUN echo "cd /compile-tree/" >> /do-build
RUN echo "if [ ! -e ./.git ]; then" >> /do-build
RUN echo "    git init ." >> /do-build
RUN echo "    git remote add origin https://github.com/horse64/core.horse64.org" >> /do-build
RUN echo "    git fetch" >> /do-build
RUN echo "    git checkout -b main -t origin/main" >> /do-build
RUN echo "    git submodule foreach git reset --hard" >> /do-build
RUN echo "    make check-submodules || git submodule update --init" >> /do-build
RUN echo "fi" >> /do-build
RUN echo "rm -f ./binaries/* && make veryclean && if [ \"x\$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** BUILDING DEBUG (for tests) ***\"; make clean debug; echo \" *** RUNNING TESTS (DEBUG)***\"; echo ""; make test; fi && make clean release && mv horsec ./binaries/horsec-x64 && chmod -R 777 /compile-tree/binaries/* && if [ \"x\$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** RUNNING TESTS (release) ***\"; echo ""; make test; fi && CC=x86_64-w64-mingw32-gcc make veryclean release && rm -f ./binaries/horsec-x64.exe && mv horsec.exe ./binaries/horsec-x64.exe && CC=aarch64-linux-gnu-gcc make veryclean release && mv horsec ./binaries/horsec-aarch64" >> /do-build
RUN chmod +x /do-build
RUN mkdir -p /compile-tree/
WORKDIR /compile-tree/

# Volume to write resulting binaries to:
VOLUME /compile-tree/binaries/

CMD /do-build
