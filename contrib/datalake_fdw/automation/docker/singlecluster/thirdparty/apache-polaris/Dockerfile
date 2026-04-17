FROM ubuntu:22.04

ARG POLARIS_VERSION=1.3.0-incubating
ARG POLARIS_TGZ=https://downloads.apache.org/incubator/polaris/${POLARIS_VERSION}/polaris-bin-${POLARIS_VERSION}.tgz
ARG POLARIS_DIR=polaris-bin-${POLARIS_VERSION}

WORKDIR /opt

# Download and unpack the binary distribution
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      openjdk-21-jre-headless \
 && rm -rf /var/lib/apt/lists/* \
 && curl -L "${POLARIS_TGZ}" -o polaris.tgz \
 && tar xzf polaris.tgz \
 && rm polaris.tgz \
 && ln -s "${POLARIS_DIR}" polaris

WORKDIR /opt/polaris

# Java env vars for clarity
ENV JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
ENV PATH="$JAVA_HOME/bin:${PATH}"

# Expose service ports and optional debug port
EXPOSE 8181 8182 5005

# Debug variables (disabled by default)
ENV JAVA_DEBUG=false \
    JAVA_DEBUG_PORT="*:5005"

# Start the Polaris server (bin script provided by the distribution)
ENTRYPOINT ["./bin/server"]
CMD ["start"]

