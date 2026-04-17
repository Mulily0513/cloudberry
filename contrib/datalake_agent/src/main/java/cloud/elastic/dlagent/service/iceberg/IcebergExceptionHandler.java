package cloud.elastic.dlagent.service.iceberg;

import cloud.elastic.dlagent.service.ServiceResult;
import lombok.extern.slf4j.Slf4j;
import org.apache.iceberg.exceptions.*;

@Slf4j
public class IcebergExceptionHandler {
    
    public static <T> ServiceResult<T> handleException(Exception e, String operation, String resource) {
        if (e instanceof NoSuchTableException) {
            log.debug("{} failed - table not found: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.notFound("table", resource + " - " + e.getMessage());
        } else if (e instanceof NoSuchNamespaceException) {
            log.debug("{} failed - namespace not found: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.notFound("namespace", resource + " - " + e.getMessage());
        } else if (e instanceof AlreadyExistsException) {
            log.warn("{} failed - resource already exists: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Resource already exists: " + resource + " - " + e.getMessage());
        } else if (e instanceof NamespaceNotEmptyException) {
            log.warn("{} failed - namespace not empty: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Namespace not empty: " + resource + " - " + e.getMessage());
        } else if (e instanceof NotAuthorizedException) {
            log.warn("{} failed - not authorized: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Not authorized: " + resource + " - " + e.getMessage());
        } else if (e instanceof ForbiddenException) {
            log.warn("{} failed - forbidden access: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Forbidden access: " + resource + " - " + e.getMessage());
        } else if (e instanceof BadRequestException) {
            log.error("{} failed - bad request: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Bad request: " + resource + " - " + e.getMessage());
        } else if (e instanceof ServiceUnavailableException) {
            log.error("{} failed - service unavailable: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Service unavailable: " + resource + " - " + e.getMessage());
        } else if (e instanceof ValidationException) {
            log.error("{} failed - validation error: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Validation error: " + resource + " - " + e.getMessage());
        } else if (e instanceof java.util.concurrent.TimeoutException) {
            log.error("{} failed - timeout: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Timeout: " + resource + " - " + e.getMessage());
        } else if (e instanceof java.io.IOException) {
            log.error("{} failed - IO error: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "IO error: " + resource + " - " + e.getMessage());
        } else if (e instanceof IllegalArgumentException) {
            log.error("{} failed - invalid arguments: {} - {}", operation, resource, e.getMessage());
            return ServiceResult.error(operation, "Invalid arguments: " + resource + " - " + e.getMessage());
        } else {
            log.error("{} failed - unexpected error: {} - {}", operation, resource, e.getMessage(), e);
            return ServiceResult.error(operation, "Unexpected error: " + resource + " - " + e.getMessage());
        }
    }
}
