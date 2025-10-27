# Dockerfile for PostgreSQL 17 with pgraft extension
# Using Debian-based image for better CGO/Go shared library compatibility
FROM postgres:17

# Install build dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libc6-dev \
    postgresql-server-dev-17 \
    golang-go \
    git \
    libjson-c-dev \
    && rm -rf /var/lib/apt/lists/*

# Set up build environment
WORKDIR /build

# Copy pgraft source code
COPY . /build/pgraft/

# Build pgraft extension
WORKDIR /build/pgraft

# Set CGO and Go build flags for optimal shared library compatibility
# CGO_ENABLED=1: Enable CGO for C interop
# CGO_CFLAGS: Optimization flags for C code
# CGO_LDFLAGS: Linker flags with proper soname
# GOFLAGS: Use netgo for pure Go networking (avoids glibc DNS issues)
ENV CGO_ENABLED=1
ENV CGO_CFLAGS="-g -O2"
ENV CGO_LDFLAGS="-Wl,-soname,pgraft_go.so"
ENV GOFLAGS="-tags=netgo"

RUN make clean && \
    make all && \
    cp pgraft.so /usr/lib/postgresql/17/lib/ && \
    cp src/pgraft_go.so /usr/lib/postgresql/17/lib/ && \
    cp pgraft.control /usr/share/postgresql/17/extension/ && \
    cp pgraft--1.0.sql /usr/share/postgresql/17/extension/ && \
    ls -lh /usr/lib/postgresql/17/lib/pgraft*.so && \
    echo "✅ pgraft extension installed successfully"

# Clean up build dependencies (optional, keeps image smaller)
# Uncomment to reduce image size after build
# RUN apt-get purge -y gcc make libc6-dev postgresql-server-dev-17 golang-go git && \
#     apt-get autoremove -y && \
#     rm -rf /var/lib/apt/lists/*

# Reset working directory
WORKDIR /

# Set default PostgreSQL data directory
ENV PGDATA=/var/lib/postgresql/data

# Expose PostgreSQL port
EXPOSE 5432

# Use the default PostgreSQL entrypoint
ENTRYPOINT ["docker-entrypoint.sh"]
CMD ["postgres"]

