#!/bin/bash

echo "🧹 Cleaning up test processes..."

# Kill processes by port
lsof -ti:8080 | xargs kill -9 2>/dev/null || true
lsof -ti:5005 | xargs kill -9 2>/dev/null || true

# Kill by process patterns
pkill -f 'dlagent-1.0.0.jar' || true
pkill -f 'spring.profiles.active=test' || true
pkill -f 'agentlib:jdwp.*address=5005' || true
pkill -f 'datalake proxy mock' || true

# Kill any remaining Java processes with test profile
ps aux | grep 'java.*test' | grep -v grep | awk '{print $2}' | xargs kill -9 2>/dev/null || true

echo "✅ Cleanup completed!"
