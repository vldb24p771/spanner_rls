FROM ubuntu:18.04
# setup ssh server
RUN apt-get update && apt-get install openssh-server -y

RUN mkdir /var/run/sshd

# SSH login fix. Otherwise user is kicked off after login
RUN sed 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' -i /etc/pam.d/sshd

RUN mkdir -p ~/.ssh && ssh-keygen -t rsa -N '' -f ~/.ssh/id_rsa && cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys


WORKDIR /root

RUN  apt-get -y update && apt-get -y install libssl-dev \
     libprotobuf-dev \
     gcc \
     make \
     build-essential \
     g++ \
     autoconf \
     pkg-config \
     libgtest-dev \
     cmake \
     inetutils-ping \
     vim \
     iproute2

RUN cd /usr/src/gtest && mkdir build && cd build  && cmake .. && make -j


COPY tapir/libevent-2.1.12-stable  /root/tapir/libevent-2.1.12-stable
COPY tapir/protobuf-3.12.0  /root/tapir/protobuf-3.12.0

RUN cd tapir/libevent-2.1.12-stable && ./configure && make -j && make install

RUN cd tapir/protobuf-3.12.0 && ./configure && make -j && make check -j && make install && ldconfig

#RUN cd tapir/tapir-master/   && make clean && make -j
