FROM almalinux:8

ENV IRODS_VERSION=4.3.1

ARG RELEASE

RUN yum install -y epel-release python3 python3-distro

RUN yum install -y wget vim jq rpmdevtools rpmrebuild make

RUN rpm --import https://packages.irods.org/irods-signing-key.asc && \
    curl -o /etc/yum.repos.d/renci-irods.yum.repo https://packages.irods.org/renci-irods.yum.repo &&\
    yum install -y irods-devel-4.3.0 irods-externals-\* gcc-c++ gdb openssl-devel

RUN yum install -y epel-release
RUN yum install -y curl libcurl-devel curl-devel openssl-devel
RUN yum install -y --enablerepo powertools librdkafka-devel

RUN mkdir /work

COPY . /work

WORKDIR /work

#RUN mkdir build && cd build && \
#    /opt/irods-externals/cmake3.11.4-0/bin/cmake -DIRODS_ENABLE_SYSLOG=1 .. && make package
RUN mkdir build && cd build && \
    /opt/irods-externals/cmake3.21.4-0/bin/cmake -DIRODS_ENABLE_SYSLOG=1 .. && make package

RUN mkdir -p /output/rpms

#RUN cp /work/build/*.rpm /output/rpms
RUN rpmrebuild -p --release=${RELEASE} --directory /output/rpms /work/build/*.rpm
#RUN rpmrebuild -p --change-spec-preamble="sed -e 's/^Version:.*/Version: ${IRODS_VERSION}/'" --release=${RELEASE} --directory /output/rpms /work/build/*.rpm
