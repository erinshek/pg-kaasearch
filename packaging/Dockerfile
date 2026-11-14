# PostgreSQL with pg_kaasearch installed and enabled in the default database.
#
#   docker build -f packaging/Dockerfile --build-arg PG_MAJOR=17 -t pg_kaasearch:17 .
#   docker run -e POSTGRES_PASSWORD=secret -p 5432:5432 pg_kaasearch:17
ARG PG_MAJOR=17

FROM postgres:${PG_MAJOR}-bookworm AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends "postgresql-server-dev-$PG_MAJOR" make gcc libc6-dev binutils \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile pg_kaasearch.control pg_kaasearch--1.0.sql ./
COPY src ./src
COPY data ./data
RUN make with_llvm=no \
 && make install with_llvm=no DESTDIR=/out \
 && find /out -name '*.so' -exec strip --strip-unneeded {} +

FROM postgres:${PG_MAJOR}-bookworm
COPY --from=build /out/ /
COPY packaging/docker-init.sql /docker-entrypoint-initdb.d/10-pg_kaasearch.sql
