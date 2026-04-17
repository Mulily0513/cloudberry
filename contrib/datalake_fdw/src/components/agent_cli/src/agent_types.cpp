#include <agent_types.hpp>

namespace agent_cli {

const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::INVALID_PARAM: return "Invalid parameter";
        case ErrorCode::NETWORK: return "Network error";
        case ErrorCode::JSON_PARSE: return "JSON parse error";
        case ErrorCode::HTTP: return "HTTP error";
        case ErrorCode::MEMORY: return "Memory error";
        case ErrorCode::CURL: return "CURL error";
        case ErrorCode::TIMEOUT: return "Timeout error";
        case ErrorCode::THREAD: return "Thread error";
        case ErrorCode::AUTH: return "Authentication error";
        case ErrorCode::NOT_FOUND: return "Not found";
        case ErrorCode::CONFLICT: return "Conflict";
        default: return "Unknown error";
    }
}

ErrorCode int_to_error_code(int code) {
    if (code >= -11 && code <= 0) {
        return static_cast<ErrorCode>(code);
    }
    return ErrorCode::INVALID_PARAM;
}

} // namespace agent_cli
