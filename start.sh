#!/bin/bash

# Start the mock backends in the background
python tools/mock_backend.py --ports 3031,3032,3033,3034 &

# Give backends a couple of seconds to boot up
sleep 2

# Deploy the C++ Load Balancer, binding it to the PORT environment variable provided by Render (defaults to 10000)
echo "Starting C++ Load Balancer on port ${PORT:-10000}..."
./cpp/simplelb --backends http://127.0.0.1:3031,http://127.0.0.1:3032,http://127.0.0.1:3033,http://127.0.0.1:3034 --port ${PORT:-10000}
