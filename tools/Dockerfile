
FROM ubuntu:18.04

RUN apt update -y && apt upgrade -y
RUN apt update -y && apt install -y wine-stable && dpkg --add-architecture i386 && apt-get -y update && apt-get install -y wine32
RUN apt update -y && apt install -y git zip bash python3 make vim gcc g++ g++-mingw-w64 gcc-mingw-w64 mingw-w64
RUN apt update && apt install -y check automake libtool autotools-dev libfreetype6-dev libxft-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev libxinerama-dev libxcb-util-dev libxext-dev libice-dev libxss-dev libxxf86dga-dev libxkbcommon-dev libgl1-mesa-dev libglew-dev libwayland-dev libx11-dev libxrandr-dev libxcursor-dev libreadline-dev
RUN apt update && apt install -y libsdl2-dev
ADD ./ /compile-tree/
RUN cd /compile-tree/ && git submodule foreach git reset --hard
RUN cd /compile-tree/ && { make check-submodules || git submodule update --init; }

VOLUME /compile-tree/binaries/

CMD bash -c "cd /compile-tree/ && { chmod -R a+rw ./binaries/* || true; } && CC=x86_64-w64-mingw32-gcc make release && rm -f ./binaries/horsec.exe && mv horsec.exe ./binaries/ && make veryclean release && rm -f ./binaries/horsec && mv horsec ./binaries/ && chmod -R 777 /compile-tree/binaries/* && if [ \"x$RUN_TESTS\" = xyes ]; then echo ""; echo \" *** RUNNING TESTS ***\"; echo ""; make test; fi"
