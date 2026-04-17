package cloud.elastic.dlagent.service;

import java.util.Map;

/**
 * Generic result class for service operations
 */
public class ServiceResult<T> {
    private final boolean success;
    private final T data;
    private final Map<String, Object> errorResponse;
    private final int statusCode;

    private ServiceResult(boolean success, T data, Map<String, Object> errorResponse, int statusCode) {
        this.success = success;
        this.data = data;
        this.errorResponse = errorResponse;
        this.statusCode = statusCode;
    }

    public static <T> ServiceResult<T> success(T data) {
        return new ServiceResult<>(true, data, null, 200);
    }

    public static <T> ServiceResult<T> notFound(String resource, String identifier) {
        Map<String, Object> errorResponse = new java.util.HashMap<>();
        Map<String, Object> error = new java.util.HashMap<>();
        error.put("message", "The given " + resource + " does not exist: " + identifier);
        error.put("type", "NoSuchTableException");
        error.put("code", 404);
        errorResponse.put("error", error);

        return new ServiceResult<>(false, null, errorResponse, 404);
    }

    public static <T> ServiceResult<T> conflict(String resource, String identifier) {
        Map<String, Object> errorResponse = new java.util.HashMap<>();
        Map<String, Object> error = new java.util.HashMap<>();
        error.put("message", "The given " + resource + " already exists: " + identifier);
        error.put("type", "AlreadyExistsException");
        error.put("code", 409);
        errorResponse.put("error", error);

        return new ServiceResult<>(false, null, errorResponse, 409);
    }

    public static <T> ServiceResult<T> error(String operation, String errorMessage) {
        Map<String, Object> errorResponse = new java.util.HashMap<>();
        Map<String, Object> error = new java.util.HashMap<>();
        error.put("message", "Error in " + operation + ": " + errorMessage);
        error.put("type", "InternalServerError");
        error.put("code", 500);
        errorResponse.put("error", error);

        return new ServiceResult<>(false, null, errorResponse, 500);
    }

    public static <T> ServiceResult<T> badRequest(String message) {
        Map<String, Object> errorResponse = new java.util.HashMap<>();
        Map<String, Object> error = new java.util.HashMap<>();
        error.put("message", message);
        error.put("type", "BadRequestException");
        error.put("code", 400);
        errorResponse.put("error", error);

        return new ServiceResult<>(false, null, errorResponse, 400);
    }

    public boolean isSuccess() {
        return success;
    }

    public T getData() {
        return data;
    }

    public Map<String, Object> getErrorResponse() {
        return errorResponse;
    }

    public int getStatusCode() {
        return statusCode;
    }
}
