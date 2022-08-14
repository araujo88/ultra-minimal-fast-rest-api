FROM ubuntu:xenial
WORKDIR /server
RUN apt-get update
RUN apt-get upgrade
RUN apt-get install libc6-dev libsqlite3-dev libsqlite0 libsqlite3-0 sqlite3 make gcc binutils cpp cpp-5 gcc-5 libasan2 libatomic1 libc-dev-bin libc6-dev libcc1-0 libcilkrts5 libgcc-5-dev libgmp10 libgomp1 libisl15 libitm1 liblsan0 libmpc3 libmpfr4 libmpx0 libquadmath0 libtsan0 libubsan0 linux-libc-dev manpages manpages-dev
COPY . /server
CMD make clean && make && ./server