# Multi-stage build:
#   Stage 1 compiles the C++20 engine/gateway/replay/benchmark binaries.
#   Stage 2 is a small runtime image that serves the Python/FastAPI demo
#   layer and ships the compiled C++ binaries alongside it.
#
# Build:  docker build -t low-latency-exchange .
# Run:    docker run -p 8000:8000 low-latency-exchange
# Then:   open http://localhost:8000

# ---------- Stage 1: build C++ binaries ----------
FROM ubuntu:24.04 AS cpp-build

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake build-essential libgtest-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY CMakeLists.txt .
COPY engine/ engine/
COPY networking/ networking/
COPY risk/ risk/
COPY concurrency/ concurrency/
COPY simulator/ simulator/
COPY benchmarks/ benchmarks/
COPY tests/ tests/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target matching_engine gateway replay bench -j"$(nproc)"

# ---------- Stage 2: runtime image ----------
FROM python:3.12-slim AS runtime

WORKDIR /app

COPY web/requirements.txt ./web/requirements.txt
RUN pip install --no-cache-dir -r web/requirements.txt

COPY web/ ./web/

COPY --from=cpp-build /src/build/matching_engine /app/bin/matching_engine
COPY --from=cpp-build /src/build/gateway /app/bin/gateway
COPY --from=cpp-build /src/build/replay /app/bin/replay
COPY --from=cpp-build /src/build/bench /app/bin/bench

ENV PORT=8000
EXPOSE 8000

WORKDIR /app/web

CMD ["sh", "-c", "/app/bin/gateway & exec uvicorn main:app --host 0.0.0.0 --port ${PORT}"]