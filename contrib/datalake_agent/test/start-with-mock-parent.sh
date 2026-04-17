#!/bin/bash

# Navigate to project root directory
cd "$(dirname "$0")/.."

# Start application with mocked parent process for testing and VS Code debugging

echo "Starting application with mocked parent process and debug mode..."
echo "Debug port: 5005"
echo ""

JAR_FILE="target/dlagent-1.0.0.jar"

if [ ! -f "$JAR_FILE" ]; then
    echo "JAR file not found: $JAR_FILE"
    echo "Building the project..."
    mvn clean package -DskipTests
fi

# Create a background process that simulates the parent
create_mock_parent() {
    # Start a background process that will act as the parent
    (
        # Create a process with "datalake proxy" in cmdline
        exec -a "datalake proxy test" sleep 3600
    ) &
    
    MOCK_PARENT_PID=$!
    echo "Created mock parent process with PID: $MOCK_PARENT_PID"
    
    # Wait a moment for the process to be created
    sleep 1
    
    # Verify the mock parent exists
    if kill -0 $MOCK_PARENT_PID 2>/dev/null; then
        echo "Mock parent process is running"
        
        # Start the application with the mock parent PID
        echo "Starting application with parent PID: $MOCK_PARENT_PID"
        echo "Application will be available on port 8080"
        echo "VS Code can attach debugger to localhost:5005"
        echo "Press Ctrl+C to stop"
        echo ""
        
        java -Xms512m \
             -Xmx1024m \
             -XX:+ExitOnOutOfMemoryError \
             -agentlib:jdwp=transport=dt_socket,server=y,suspend=n,address=5005 \
             -Dspring.main.banner-mode=off \
             -Dlogging.level.root=WARN \
             -Dlogging.level.cn.cbdb.dlagent=DEBUG \
             -Dlogging.pattern.console="%d{HH:mm:ss.SSS} [%thread] %-5level %logger{36} - %msg%n" \
             -Dspring.jmx.enabled=false \
             -Dspring.profiles.active=test \
             -jar "$JAR_FILE" \
             --parent.pid=$MOCK_PARENT_PID \
             --logging.config= \
             --server.port=8080
        
        # Clean up mock parent when application exits
        kill $MOCK_PARENT_PID 2>/dev/null
    else
        echo "Failed to create mock parent process"
        exit 1
    fi
}

echo "Found JAR file: $JAR_FILE"
create_mock_parent
