#!/bin/bash

cd "$(dirname "$0")/.."

echo "🧪 Running integration tests..."

# Run the integration tests
mvn test -Dtest=IcebergRestControllerIntegrationTest

echo "✅ Integration tests completed!"
