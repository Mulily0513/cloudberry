-- scale_concurrency_note.sql
-- Placeholder: Concurrency load tests require pgbench and cannot run inside
-- pg_regress. The actual pgbench workload scripts will live in
-- sqlrepo/scale/concurrency_load/scripts/.
--
-- This file exists so the test suite has a runnable entry point that confirms
-- the framework is wired up correctly.

\i ../../../lib/sql/common_setup.sql
\i ../../../lib/sql/performance_helpers.sql

SELECT test_log('Concurrency load tests require pgbench - see scripts/');
