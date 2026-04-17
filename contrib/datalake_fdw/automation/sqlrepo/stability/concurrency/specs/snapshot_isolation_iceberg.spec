# snapshot_isolation_iceberg.spec
#
# Verify Iceberg snapshot isolation: s1 captures a read snapshot before s2
# writes; s1's subsequent reads should still return the pre-write data until
# s1 ends its transaction.
#
# REQUIRES: iceberg_catalog_fdw and iceberg_volume_fdw available,
# lakehouse S3 service reachable, a warehouse bucket at lakehouse:9100.
#
# NOT YET ENABLED in Makefile - uncomment after expected/ is populated.

setup
{
    CREATE EXTENSION IF NOT EXISTS datalake_fdw;

    CREATE SERVER iso_cat_srv FOREIGN DATA WRAPPER iceberg_catalog_fdw;
    CREATE USER MAPPING FOR current_user SERVER iso_cat_srv;
    CREATE FOREIGN CATALOG iso_cat SERVER iso_cat_srv;
    SET iceberg_default_catalog = 'iso_cat';

    CREATE SERVER iso_vol_srv FOREIGN DATA WRAPPER iceberg_volume_fdw
    OPTIONS (type 's3', endpoint 'http://lakehouse:9100', region 'us-east-1',
             bucket_name 'warehouse', path_style_access 'true');
    CREATE USER MAPPING FOR current_user SERVER iso_vol_srv
    OPTIONS (access_key_id 'admin', secret_access_key 'password');
    CREATE FOREIGN VOLUME iso_vol SERVER iso_vol_srv
    OPTIONS (base_path '/iso_test/');
    SET iceberg_default_volume = 'iso_vol';

    CREATE ICEBERG TABLE iso_tbl (id int, val numeric(10,2));
    INSERT INTO iso_tbl SELECT i, i * 1.5 FROM generate_series(1, 50) AS t(i);
}

teardown
{
    DROP TABLE IF EXISTS iso_tbl;
    DROP VOLUME IF EXISTS iso_vol;
    DROP USER MAPPING IF EXISTS FOR current_user SERVER iso_vol_srv;
    DROP SERVER IF EXISTS iso_vol_srv;
    DROP CATALOG IF EXISTS iso_cat;
    DROP USER MAPPING IF EXISTS FOR current_user SERVER iso_cat_srv;
    DROP SERVER IF EXISTS iso_cat_srv;
}

session "s1_reader"
setup { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "r_initial" { SELECT count(*) FROM iso_tbl; }
step "r_after"   { SELECT count(*) FROM iso_tbl; }
step "r_commit"  { COMMIT; }

session "s2_writer"
step "w_insert"  { INSERT INTO iso_tbl SELECT i, i * 1.5
                   FROM generate_series(51, 100) AS t(i); }

# Expected: s1 sees 50 rows both times (pre-write snapshot),
# while s2 adds 50 rows between the two reads.
permutation "r_initial" "w_insert" "r_after" "r_commit"
