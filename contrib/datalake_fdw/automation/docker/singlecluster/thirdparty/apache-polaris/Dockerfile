FROM ubuntu:22.04

# TARGETARCH is auto-populated by Docker Buildx (amd64 / arm64). Default is
# inferred from the host arch when building with legacy `docker build`.
ARG TARGETARCH

ARG POLARIS_VERSION=1.3.0-incubating
ARG POLARIS_TGZ=https://downloads.apache.org/incubator/polaris/${POLARIS_VERSION}/polaris-bin-${POLARIS_VERSION}.tgz
ARG POLARIS_DIR=polaris-bin-${POLARIS_VERSION}

WORKDIR /opt

# Copy the build context to a scratch location so we can conditionally pick up
# a pre-downloaded polaris.tgz. build-bundle.sh drops the tarball here before
# invoking buildx; a plain `docker compose build` will have only Dockerfile in
# the context and the RUN below falls back to curl.
COPY . /tmp/polaris-context/

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      openjdk-21-jre-headless \
 && rm -rf /var/lib/apt/lists/* \
 && if [ -s /tmp/polaris-context/polaris.tgz ]; then \
      echo "[cache] using pre-downloaded polaris.tgz from build context"; \
      mv /tmp/polaris-context/polaris.tgz polaris.tgz; \
    else \
      echo "[net] downloading polaris ${POLARIS_VERSION} from ${POLARIS_TGZ}"; \
      curl -fL --retry 3 --retry-delay 5 "${POLARIS_TGZ}" -o polaris.tgz; \
    fi \
 && tar xzf polaris.tgz \
 && rm polaris.tgz \
 && rm -rf /tmp/polaris-context \
 && ln -s "${POLARIS_DIR}" polaris

WORKDIR /opt/polaris

# Resolve JAVA_HOME from the Debian architecture so the image works on both
# x86_64 (amd64) and aarch64 (arm64) hosts.
ENV JAVA_HOME=/usr/lib/jvm/java-21-openjdk-${TARGETARCH}
ENV PATH="$JAVA_HOME/bin:${PATH}"

# Expose service ports and optional debug port
EXPOSE 8181 8182 5005

# Debug variables (disabled by default)
ENV JAVA_DEBUG=false \
    JAVA_DEBUG_PORT="*:5005"

# Start the Polaris server (bin script provided by the distribution)
ENTRYPOINT ["./bin/server"]
CMD ["start"]
