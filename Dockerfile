FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    cmake ninja-build g++-12 \
    libyaml-cpp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=g++-12 \
      -G Ninja \
    && cmake --build build --parallel

# ── Runtime image ────────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y libyaml-cpp0.7 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/mimir     ./mimir
COPY --from=builder /src/build/mimir-cli ./mimir-cli
COPY config.yaml .

RUN mkdir -p data

EXPOSE 6379
VOLUME ["/app/data"]

CMD ["./mimir", "config.yaml"]
