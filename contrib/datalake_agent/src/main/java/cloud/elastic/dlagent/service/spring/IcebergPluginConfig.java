package cloud.elastic.dlagent.service.spring;

import cloud.elastic.dlagent.plugins.iceberg.IcebergCatalogWrapper;
import cloud.elastic.dlagent.plugins.iceberg.IcebergMetadataFetcher;
import cloud.elastic.dlagent.plugins.iceberg.utilities.IcebergUtilities;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/**
 * Spring configuration for Iceberg plugin beans
 */
@Configuration
public class IcebergPluginConfig {

    @Autowired
    private IcebergUtilities icebergUtilities;

    @Autowired
    private IcebergCatalogWrapper icebergCatalogWrapper;

    @Bean
    public IcebergMetadataFetcher icebergMetadataFetcher() {
        return new IcebergMetadataFetcher(icebergUtilities, icebergCatalogWrapper);
    }
}
