package cloud.elastic.dlagent.plugins.iceberg;

import java.util.Map;

/**
 * Factory interface for creating Iceberg catalogs
 */
public interface IcebergCatalogFactory {
    
    /**
     * Create an Iceberg catalog based on catalog type and configuration
     *
     * @param catalogType Type of catalog (hive, hadoop, s3, polaris)
     * @param catalogConfig Catalog configuration properties
     * @param volumeConfig Volume configuration properties
     * @return Initialized IcebergCatalog
     */
    IcebergCatalog createCatalog(String catalogType, Map<String, String> catalogConfig, Map<String, String> volumeConfig);
}
