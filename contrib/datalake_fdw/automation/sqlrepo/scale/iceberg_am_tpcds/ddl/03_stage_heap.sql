-- 03_stage_heap.sql
-- Staging heap mirror of the 24 iceberg tables. We use CREATE TABLE (LIKE
-- public.<tname>) so column types are defined exactly once (in
-- 02_iceberg_am.sql). The stage is AO column-store for fast COPY + scan;
-- DISTRIBUTED RANDOMLY to avoid pinning per-table distribution keys for
-- 24 tables — the follow-up INSERT ... SELECT into iceberg still
-- parallelises across segments.

DROP SCHEMA IF EXISTS stage CASCADE;
CREATE SCHEMA stage;

CREATE TABLE stage.call_center            (LIKE public.call_center)            WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.catalog_page           (LIKE public.catalog_page)           WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.catalog_returns        (LIKE public.catalog_returns)        WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.catalog_sales          (LIKE public.catalog_sales)          WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.customer               (LIKE public.customer)               WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.customer_address       (LIKE public.customer_address)       WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.customer_demographics  (LIKE public.customer_demographics)  WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.date_dim               (LIKE public.date_dim)               WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.household_demographics (LIKE public.household_demographics) WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.income_band            (LIKE public.income_band)            WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.inventory              (LIKE public.inventory)              WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.item                   (LIKE public.item)                   WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.promotion              (LIKE public.promotion)              WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.reason                 (LIKE public.reason)                 WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.ship_mode              (LIKE public.ship_mode)              WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.store                  (LIKE public.store)                  WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.store_returns          (LIKE public.store_returns)          WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.store_sales            (LIKE public.store_sales)            WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.time_dim               (LIKE public.time_dim)               WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.warehouse              (LIKE public.warehouse)              WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.web_page               (LIKE public.web_page)               WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.web_returns            (LIKE public.web_returns)            WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.web_sales              (LIKE public.web_sales)              WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
CREATE TABLE stage.web_site               (LIKE public.web_site)               WITH (appendonly=true, orientation=column, compresstype=zstd) DISTRIBUTED RANDOMLY;
