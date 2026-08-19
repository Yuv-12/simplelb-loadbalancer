# Stage 1: Build the C++ Load Balancer
FROM gcc:12 AS builder
WORKDIR /app
COPY cpp/src/main.cpp ./
COPY cpp/src/load_balancer.cpp ./
COPY cpp/src/load_balancer.h ./
RUN g++ -O3 -std=c++11 main.cpp load_balancer.cpp -o simplelb -lpthread

# Stage 2: Final Runtime Environment (Python + compiled C++ binary)
FROM python:3.11-slim
WORKDIR /app

# Copy compiled C++ binary
COPY --from=builder /app/simplelb ./cpp/simplelb

# Copy mock backends, test client and start script
COPY tools/ ./tools/
COPY start.sh .

# Make start script executable
RUN chmod +x start.sh

# Expose port (default Render port is 10000)
EXPOSE 10000

# Start script
ENTRYPOINT [ "./start.sh" ]
