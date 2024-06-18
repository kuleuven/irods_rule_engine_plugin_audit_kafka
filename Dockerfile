FROM registry.icts.kuleuven.be/icts/icts-centos9:latest AS base

ARG MAIN_BUILD
ARG REVISION

RUN echo -e "[irods]\nname=irods\nbaseurl=https://repo.icts.kuleuven.be/artifactory/irods-builds/${MAIN_BUILD}\nenabled=1\ngpgcheck=0" > /etc/yum.repos.d/irods.repo

RUN dnf install -y epel-release python3 python3-distro
RUN dnf install -y wget vim jq rpmdevtools rpmrebuild make
RUN dnf install -y irods-devel irods-externals-\* gcc-c++ gdb openssl-devel
RUN dnf install -y --disablerepo=* --enablerepo=baseos,appstream,extras curl-devel
RUN yum install -y --enablerepo crb librdkafka-devel

RUN mkdir /work

COPY . /work

WORKDIR /work

RUN sed -i 's/set(IRODS_PLUGIN_REVISION "0")/set(IRODS_PLUGIN_REVISION "'$REVISION'")/' CMakeLists.txt
RUN sed -i 's/set(IRODS_PACKAGE_REVISION "0")/set(IRODS_PACKAGE_REVISION "'$MAIN_BUILD'")/' CMakeLists.txt

RUN mkdir build && cd build && \
    /opt/irods-externals/cmake3.21.4-0/bin/cmake .. && make package

RUN mkdir -p /output/rpms

RUN rpmrebuild -p --directory /output/rpms /work/build/*.rpm
