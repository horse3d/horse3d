
FROM ubuntu:18.04

RUN apt update -y && apt upgrade -y
RUN apt update -y && apt install -y wine-stable && dpkg --add-architecture i386 && apt-get -y update && apt-get install -y wine32
RUN apt update -y && apt install -y git zip bash python3 make vim gcc g++ g++-mingw-w64 gcc-mingw-w64 mingw-w64
RUN apt update && apt install -y automake libtool autotools-dev libfreetype6-dev libxft-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev libxinerama-dev libxcb-util-dev libxext-dev libice-dev libxss-dev libxxf86dga-dev libxkbcommon-dev libgl1-mesa-dev libglew-dev libwayland-dev libx11-dev libxrandr-dev libxcursor-dev libreadline-dev
RUN apt update && apt install -y libsdl2-dev
ADD ./ /compile-tree/
RUN cd /compile-tree/ && git submodule foreach git reset --hard

VOLUME /compile-tree/releases/

CMD bash -c "cd /compile-tree/ && chmod -R a+rw /compile-tree/releases/* && make releases && chmod -R 777 /compile-tree/releases/* && chmod -x /compile-tree/releases/Horse3D-*/*.h3dpak"
