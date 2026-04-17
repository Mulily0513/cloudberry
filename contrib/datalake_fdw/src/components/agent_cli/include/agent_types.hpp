#ifndef AGENT_TYPES_HPP
#define AGENT_TYPES_HPP

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace agent_cli {

// Exception for cancelled operations
class CancelledException : public std::runtime_error {
public:
    CancelledException() : std::runtime_error("Operation cancelled by user interrupt") {}
};

// Forward declarations
class AgentClient;

// Internal context structure
struct AgentContext {
    std::unique_ptr<AgentClient> client;
    std::string last_error;
    int debug_level = 0;
    
    AgentContext() = default;
    ~AgentContext() = default;
    
    // Non-copyable
    AgentContext(const AgentContext&) = delete;
    AgentContext& operator=(const AgentContext&) = delete;
};

// HTTP header structure
struct HttpHeader {
    std::string key;
    std::string value;
};

// Error codes mapping
enum class ErrorCode {
    SUCCESS = 0,
    INVALID_PARAM = -1,
    NETWORK = -2,
    JSON_PARSE = -3,
    HTTP = -4,
    MEMORY = -5,
    CURL = -6,
    TIMEOUT = -7,
    THREAD = -8,
    AUTH = -9,
    NOT_FOUND = -10,
    CONFLICT = -11,
    BAD_REQUEST = 12
};

// Utility functions
const char* error_code_to_string(ErrorCode code);
ErrorCode int_to_error_code(int code);

} // namespace agent_cli

#endif // AGENT_TYPES_HPP
