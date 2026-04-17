package cloud.elastic.dlagent.service.spring;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

/**
 * Configuration properties for Iceberg REST API
 */
@Configuration
@ConfigurationProperties(prefix = "iceberg.rest")
@Data
public class IcebergRestConfig {

    /**
     * Whether Iceberg REST API is enabled
     */
    private boolean enabled = true;

    /**
     * Base path for Iceberg REST API
     */
    private String basePath = "/api";

    /**
     * Default target file size in MB for vacuum compaction.
     * Used as fallback when the caller does not supply this parameter.
     * Should be kept in sync with GUC datalake.iceberg_vacuum_rewrite_target_file_size_mb.
     */
    private int vacuumTargetFileSizeMb = 512;

    /**
     * Default minimum number of input files for vacuum compaction.
     * Used as fallback when the caller does not supply this parameter.
     * Should be kept in sync with GUC datalake.iceberg_vacuum_compact_min_input_files.
     */
    private int vacuumMinInputFiles = 5;
}
