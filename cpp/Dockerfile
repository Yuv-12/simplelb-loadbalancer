FROM gcc:12 AS builder
WORKDIR /app
COPY src/ ./src/
RUN g++ -O3 -std=c++11 src/main.cpp src/load_balancer.cpp -o simplelb -lpthread

FROM debian:bookworm-slim
WORKDIR /root
COPY --from=builder /app/simplelb .
ENTRYPOINT [ "./simplelb" ]
