# Agent CLI

C/C++ client library for datalake_agent REST API.

## Build

### Release (default)
```bash
mkdir build && cd build
cmake ..
make -j4
```

### Debug
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j4
```

Generated libraries:
- Release: `libagent_cli.so`, `libagent_cli.a`
- Debug: `libagent_cli_debug.so`, `libagent_cli_debug.a`

## Usage

### C++
```cpp
#include <agent_client.hpp>

agent_cli::Config config;
config.server_url = "http://localhost:8080";
config.prefix = "iceberg";

agent_cli::AgentClient client(config);
auto resp = client.load_table("namespace", "table");
```

### C
```c
#include <agent_c_api.h>

agent_cli_config_t config = {};
config.server_url = "http://localhost:8080";
config.prefix = "iceberg";

agent_cli_handle_t handle;
agent_cli_init(&config, &handle);

agent_cli_response_t response = {};
agent_cli_load_table(handle, "namespace", "table", &response);
agent_cli_free_response(&response);
agent_cli_cleanup(handle);
```

## API

- `create_table` - Create table
- `load_table` - Load table metadata  
- `table_exists` - Check table exists
- `get_fragment` - Get fragments
- `append_table` - Append data

## Test

```bash
./test_stable
```

## Dependencies

- libcurl
- CMake 3.10+
- C++14
