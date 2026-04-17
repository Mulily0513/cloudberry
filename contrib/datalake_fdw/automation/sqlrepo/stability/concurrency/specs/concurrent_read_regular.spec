# concurrent_read_regular.spec
#
# Two sessions read the same regular table concurrently. This spec does NOT
# use Iceberg - it exists to verify the pg_isolation_regress wiring itself
# works end-to-end. If this spec passes but the iceberg-* specs fail, the
# issue is Iceberg-specific (catalog, volume, S3) not the isolation harness.

setup
{
    CREATE TABLE conc_read_tbl (id int PRIMARY KEY, val text);
    INSERT INTO conc_read_tbl
    SELECT i, 'row_' || i FROM generate_series(1, 100) AS t(i);
}

teardown
{
    DROP TABLE conc_read_tbl;
}

session "s1"
step "s1_count"  { SELECT count(*) FROM conc_read_tbl; }
step "s1_sum"    { SELECT sum(id) FROM conc_read_tbl; }

session "s2"
step "s2_count"  { SELECT count(*) FROM conc_read_tbl; }
step "s2_filter" { SELECT count(*) FROM conc_read_tbl WHERE id > 50; }

# Two permutations to cover both orderings
permutation "s1_count" "s2_count" "s1_sum" "s2_filter"
permutation "s2_count" "s1_count" "s2_filter" "s1_sum"
