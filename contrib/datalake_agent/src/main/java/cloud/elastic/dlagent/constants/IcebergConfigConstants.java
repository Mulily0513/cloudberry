package cloud.elastic.dlagent.constants;

/**
 * Configuration constants for Iceberg REST API
 * Following the OpenAPI schema naming convention
 */
public final class IcebergConfigConstants {

    private IcebergConfigConstants() {
        // Utility class
    }

    public static final String ICEBERG_CONFIG = "IcebergConfig";

    public static final class ICEBERG_CATALOG_CONFIG {
        public static final String ICEBERG_CATALOG_CONFIG_STRING = "IcebergCatalogConfig";
        public static final String SERVER_TYPE = "server_type";
        public static final String HIVE_METASTORE_URI = "hive_metastore_uri";
        public static final String USERNAME = "username";
        public static final String AUTH_METHOD = "auth_method";
        public static final String KRB_SERVICE_PRINCIPAL = "krb_service_principal";
        public static final String KRB_CLIENT_PRINCIPAL = "krb_client_principal";
        public static final String KRB_CLIENT_KEYTAB = "krb_client_keytab";
        public static final String CATALOG_NAME = "catalog_name";
        public static final String ENABLE_METADATA_CACHE = "enable_metadata_cache";
        public static final String METADATA_CACHE_TTL = "metadata_cache_ttl";
        public static final String AUTO_REFRESH_METADATA = "auto_refresh_metadata";
        public static final String WAREHOUSE_LOCATION_PERFIX = "warehouse_location_prefix";
        public static final String POLARIS_SERVER_URL = "polaris_server_url";
        public static final String CLIENT_ID = "client_id";
        public static final String CLIENT_SECRET = "client_secret";
        public static final String SCOPE = "scope";
    }

    //builtin catalog rest api option
    public static final class BUILDIN_CATALOG_OPTION {
        public static final String BUILDIN_CATALOG_STRING = "buildInCatalog";
        public static final String TABLE_EXISTS_PROP = "table_exists";
        public static final String METADATA_LOCATION_PROP = "metadata_location";
    }

    public static final String CATALOG_TYPE_HIVE = "hive";
    public static final String CATALOG_TYPE_S3A = "s3a";
    public static final String CATALOG_TYPE_S3 = "s3";
    public static final String CATALOG_TYPE_BUILDIN = "builtin";
    public static final String CATALOG_TYPE_POLARIS = "polaris";

    public static final class ICEBERG_VOLUME_CONFIG {
        public static final String ICEBERG_VOLUME_CONFIG_STRING = "IcebergVolumeConfig";
        public static final String VOLUME_SERVER_TYPE = "volume_server_type";
        public static final String VOLUME_ENDPOINT = "volume_endpoint";
        public static final String VOLUME_REGION = "volume_region";
        public static final String BUCKET_NAME = "bucket_name";
        public static final String PATH_STYLE_ACCESS = "path_style_access";
        public static final String ACCESS_KEY_ID = "access_key_id";
        public static final String SECRET_ACCESS_KEY = "secret_access_key";
        public static final String BASE_PATH = "base_path";
        public static final String ENABLE_CACHING = "enable_caching";
        public static final String ALLOW_WRITES = "allow_writes";
        public static final String USERNAME = "uername";
        // It may be deprecated in the future, or it could be configured as a simplified parameter. It is not currently in use.
        public static final String CATALOG_FILE_IO_IMPL = "catalog_file_io_impl";
    }

    // FileIO implementation values
    public static final String GOPHER_FILE_IO = "GopherFileIO";
    public static final String DEFAULT_IMPL_VALUE = "default";
    public static final String CUSTOM_IMPL_VALUE = "customFileIO";

    // Hadoop configuration keys
    public static final String FS_S3A_IMPL = "fs.s3a.impl";
    public static final String FS_S3A_AWS_CREDENTIALS_PROVIDER = "fs.s3a.aws.credentials.provider";
    public static final String FS_S3A_ACCESS_KEY = "fs.s3a.access.key";
    public static final String FS_S3A_SECRET_KEY = "fs.s3a.secret.key";
    public static final String FS_S3A_ENDPOINT = "fs.s3a.endpoint";
    public static final String FS_S3A_PATH_STYLE_ACCESS = "fs.s3a.path.style.access";

    // Hadoop implementation values
    public static final String S3A_FILESYSTEM_IMPL = "org.apache.hadoop.fs.s3a.S3AFileSystem";
    public static final String S3A_CREDENTIALS_PROVIDER = "org.apache.hadoop.fs.s3a.SimpleAWSCredentialsProvider";

    // S3FileIO
    public static final String S3FILEIO_ACCESS_KEY_ID = "s3.access-key-id";
    public static final String S3FILEIO_SECRET_ACCESS_KEY = "s3.secret-access-key";
    public static final String S3FILEIO_ENDPOINT = "s3.endpoint";
    public static final String S3FILEIO_REGION = "s3.region";
    public static final String S3FILEIO_PATH_STYLE_ACCESS = "s3.path-style-access";

    // S3 Default value
    public static final String DEFAULT_S3_REGION_VALUE = "us-east-1";

    // Volume server types — must match FDW's iceberg_volume_option.c definitions
    public static final String VOLUME_TYPE_S3A = "s3a";
    public static final String VOLUME_TYPE_S3 = "s3";
    public static final String VOLUME_TYPE_HDFS = "hdfs";
    public static final String VOLUME_TYPE_ABFSS = "abfss";

    // Configuration key prefixes
    public static final String FILE_IO_CONFIG_IMPL_CLASS = "FileIOConfig.impl_class";
    public static final String FILE_IO_CONFIG_PROPERTIES_PREFIX = "FileIOConfig.properties";
    public static final String FILE_IO_CONFIG_PROPERTIES_IOIMPL = "FileIOConfig.properties.impl_class";
    public static final String GOPHER_COMMON_CONFIG_PREFIX = "GopherCommonConfig";

    //impl class
    public static final String HADOOP_FILE_IO_CLASS_NAME = "org.apache.iceberg.hadoop.HadoopFileIO";
    public static final String S3_FILE_IO_CLASS_NAME = "org.apache.iceberg.aws.s3.S3FileIO";
    public static final String ICEBERG_S3_FILE_IO_CLASS_NAME = "org.apache.iceberg.io.ResolvingFileIO";

    public static final String ICEBERG_FILE_IO_CLASS_NAME = "org.apache.iceberg.io.ResolvingFileIO";
    public static final String GOPHER_FILE_IO_CLASS_NAME = "cloud.elastic.dlagent.plugins.iceberg.GopherFileIO";
    // Common field names
    public static final String COMMON = "common";
    public static final String PROPERTIES = "properties";

    public static final class ICEBERG_ADDITIONAL_CONFIG {
        public static final String ICEBERG_ADDITIONAL_CONFIG_STRING = "IcebergAdditionalConfig";
        public static final String TOTAL_SEGMENT = "totalSegment";
        public static final String SPLIT_SIZE = "splitSize";
        public static final String FILTER_STRING = "filterString";
        public static final String FILE_IO_CONFIG = "fileIOConfig";
        public static final String TABLE_IDENTIFIER = "tableIdentifier";
    }

    public static final class FILE_IO_CONFIG {
        public static final String SIMPLE_FILEIO_CONFIG = "simpleFileIOConfig";
        public static final String GOPHER_FILEIO_CONFIG = "gopherFileIOConfig";
        public static final String IMPL_CLASS = "impl_class";
    }

    public static final class GOPHER_FILE_IO_CONFIG {
        public static final String GOPHER_FILE_IO_CONFIG_STRING = "GopherFileIOConfig";

        public static final String PROPERTIES = "properties";
    }

    public static final class GOPHER_CONFIG {
        public static final String GOPHER_HEADER = "gopher";
        public static final String GOPHER_CONFIG_STRING = "gopherConfig";
        public static final String WORKER_PATH = "worker_path";
        public static final String CONNECT_PATH = "connect_path";
        public static final String CONNECT_PLASMA_PATH = "connect_plasma_path";
        public static final String UFS_TYPE = "ufs_type";
        public static final String URI_PREFIX = "uri_prefix";
        public static final String CACHE_STRATEGY = "cache_strategy";
        public static final String GOPHER_MODE = "gopher_mode";
        public static final String LOG_LEVEL = "log_level";
        public static final String LIBOSS2_LOG_SEVERITY = "liboss2_log_severity";
        public static final String CACHE_PREDICT_NUM = "cache_predict_num";
        public static final String LOCAL_PATH = "local_path";
        public static final String LOAD_LIBGOPHER_CLIENT_PATH = "load_libgopherClient_path";
    }
}
