
# Copyright (c) 2024-2024, MDB, Mother Russia

# Minimal test testing streaming replication
use strict;
use warnings;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

# Initialize primary node
my $node_primary = PostgreSQL::Test::Cluster->new('primary');
$node_primary->init();
$node_primary->start;

# Create some content on primary and check its presence in standby nodes
$node_primary->safe_psql('postgres',
	"
    CREATE DATABASE regress;
    CREATE ROLE mdb_admin;
    CREATE ROLE mdb_reg_lh_1;
    CREATE ROLE mdb_reg_lh_2;
    GRANT pg_signal_backend TO mdb_admin;
    GRANT pg_signal_backend TO mdb_reg_lh_1;
    GRANT mdb_admin TO mdb_reg_lh_2;
");

# Create some content on primary and check its presence in standby nodes
$node_primary->safe_psql('regress',
	"
    CREATE TABLE tab_int(i int);
    INSERT INTO tab_int SELECT * FROm generate_series(1, 1000000);
    ALTER SYSTEM SET autovacuum_vacuum_cost_limit TO 1;
    ALTER SYSTEM SET autovacuum_vacuum_cost_delay TO 100;
    ALTER SYSTEM SET autovacuum_naptime TO 1;    
");

$node_primary->restart;

sleep 1;

my $res_pid = $node_primary->safe_psql('regress',
	"
    select pg_backend_pid();
");

my ($res_reg_lh_1, $stdout_reg_lh_1, $stderr_reg_lh_1)  = $node_primary->psql('regress',
	"
    SET ROLE mdb_reg_lh_1;
    SELECT pg_terminate_backend($res_pid);
");


like($stderr_reg_lh_1, qr/WARNING:.*not a PostgreSQL server process/, "matches expected warning");

done_testing();
