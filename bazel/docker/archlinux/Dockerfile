# Create a virtual environment with all tools installed
# ref: https://hub.docker.com/_/archlinux/
FROM archlinux/base AS env
LABEL maintainer="corentinl@google.com"
# Install system build dependencies
ENV PATH=/usr/local/bin:$PATH
RUN pacman -Syu --noconfirm git base-devel bazel
CMD [ "/bin/bash" ]

FROM env AS devel
WORKDIR /home/lib
COPY . .

FROM devel as build
RUN bazel build --curses=no --copt='-Wno-sign-compare' //...:all

FROM build as test
RUN bazel test -c opt --curses=no --copt='-Wno-sign-compare' //...:all
