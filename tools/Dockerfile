
FROM ubuntu:18.04

# Upgrade image in general:
RUN apt update -y && apt upgrade -y

# Install wine (used to run premake.exe for bullet physics cross-platform build):
RUN apt update -y && apt install -y wine-stable && dpkg --add-architecture i386 && apt-get -y update && apt-get install -y wine32

# Install various tools like git, python3, and more:
RUN apt update -y && apt install -y git zip valgrind bash python3 make sed vim gcc g++ g++-mingw-w64 gcc-mingw-w64 mingw-w64 gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Install needed base system libraries:
RUN apt update && apt install -y check automake libtool autotools-dev libfreetype6-dev libxft-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev libxinerama-dev libxcb-util-dev libxext-dev libice-dev libxss-dev libxxf86dga-dev libxkbcommon-dev libgles2-mesa-dev libgl1-mesa-dev libglew-dev libwayland-dev libwayland-egl1 kwayland-integration libkf5waylandclient5 libx11-dev libxrandr-dev libxcursor-dev libreadline-dev

# Install SDL2 (won't be linked, but bad SDL2_TTF build scripts currently need this):
RUN apt update && apt install -y libsdl2-dev

# Add in our project code and make sure submodules are initialized:
ADD ./ /compile-tree/
RUN cd /compile-tree/ && git submodule foreach git reset --hard
RUN cd /compile-tree/ && { make check-submodules || git submodule update --init; }

# Hack because SDL_TTF's build script is a bit trash:
RUN rm -rf /usr/include/SDL2 && ln -s /compile-tree/vendor/SDL-mirror/include/ /usr/include/SDL2

# Volume to write resulting binaries to:
VOLUME /compile-tree/binaries/

# Command to build release builds, and optionally run tests:
#CMD bash -c "cd /compile-tree/ && rm -f ./binaries/* && CC=x86_64-w64-mingw32-gcc make release && rm -f ./binaries/horsec-x64.exe && mv horsec.exe ./binaries/horsec-x64.exe && CC=aarch64-linux-gnu-gcc make veryclean release && mv horsec ./binaries/horsec-aarch64 && make veryclean release && mv horsec ./binaries/horsec-x64 && chmod -R 777 /compile-tree/binaries/* && if [ \"x$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** RUNNING TESTS ***\"; echo ""; make test; fi"
CMD bash -c "cd /compile-tree/ && rm -f ./binaries/* && CC=x86_64-w64-mingw32-gcc make release && rm -f ./binaries/horsec-x64.exe && mv horsec.exe ./binaries/horsec-x64.exe && make veryclean release && mv horsec ./binaries/horsec-x64 && chmod -R 777 /compile-tree/binaries/* && if [ \"x$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** RUNNING TESTS ***\"; echo ""; make test; fi"
