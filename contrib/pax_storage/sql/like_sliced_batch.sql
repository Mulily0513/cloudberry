-- Regression test for three vec-parallel correctness bugs in the LIKE /
-- MIN(string) path. All three bugs are exposed under pool_threads > 0 and
-- were silent (wrong result or crash) before the fix.
--
-- 1. MatchSubstringImpl end_data offset (scalar_string.cc)
--    end_data = start_data + offsets[length] double-counts offsets[0] and
--    overshoots the data buffer on sliced batches -> SEGV.
--
-- 2. shift[256] global-static race (scalar_string.cc)
--    Boyer-Moore-Horspool shift table was a file-scope static shared across
--    concurrent LIKE kernels. Two LIKE filters on different patterns
--    (Title LIKE + URL NOT LIKE) running on different executor threads would
--    trample each other's shift table mid-scan, producing dropped matches
--    non-deterministically.
--
-- 3. GroupedStringMinMaxImpl::Merge empty-initial-state (hash_aggregate.cc)
--    Merge used `other < string_view(mins_[g])` without checking whether the
--    destination slot had been initialized. Since an uninitialized slot holds
--    an empty string which is the MIN under byte-wise ordering, any non-empty
--    value from `other` never won the comparison -> MIN(string) silently
--    returned empty when per-thread partial states were merged (triggered by
--    two-phase aggregation for COUNT(DISTINCT)).
--
-- Note on COLLATE "C": vec uses byte-wise string comparison (memcmp), while
-- PG's default collation is locale-aware. To keep this test deterministic
-- across vectorization on/off modes, we either use ASCII-only data (where
-- both orderings agree) or explicitly COLLATE "C".

set vector.enable_vectorization = on;
set vector.pool_threads = 4;

drop table if exists pax_test.like_slice_t;
create table pax_test.like_slice_t (
    id    int,
    grp   int,
    uid   int,
    url   text,
    title text
) using pax;

-- 200K rows with:
--  - ASCII-only url/title so byte-wise MIN matches locale MIN,
--  - a small grp (100 groups) with many rows each so HashAgg merges
--    meaningful per-thread partial states,
--  - uid drawn from a modest domain so COUNT(DISTINCT uid) is non-trivial
--    and forces two-phase aggregation.
insert into pax_test.like_slice_t
select
    i,
    i % 100,
    i % 5000,
    'http://example' || lpad((i % 1000)::text, 4, '0') || '.com/path/'
        || repeat('x', (i % 97) + 1)
        || case when i % 7 = 0 then '.google.' else '' end,
    'Title ' || lpad(i::text, 7, '0') || ' '
        || case when i % 11 = 0 then 'Google search'
                when i % 13 = 0 then 'other engine'
                else 'nothing special' end
from generate_series(1, 200000) i;

-- Bug 1: crash on sliced LIKE. Two LIKE predicates on text columns.
select count(*) as total
from pax_test.like_slice_t
where title like '%Google%'
  and url not like '%.google.%';

-- Bug 1 again: short-substring LIKE (memchr fast path) with filter-sliced batch.
select count(*) as filtered_short_like
from pax_test.like_slice_t
where id > 10000
  and title like '%Go%';

-- Bug 2: shift[256] race under two concurrent LIKE kernels. Running this
-- several times would have produced varying counts with the old global shift
-- table; now it must be deterministic.
select count(*) as two_like_race
from pax_test.like_slice_t
where title like '%Google search%'
  and url like '%example0001.com%';

-- Bug 3: MIN(string) + COUNT(DISTINCT) + GROUP BY triggers two-phase
-- HashAggregate. Per-thread partial states must merge correctly - previously
-- MIN(url)/MIN(title) came back empty for every group in parallel mode.
-- We sum the LENGTH of MIN to get a single scalar that depends on every
-- group's MIN being populated, so "any group missing MIN" manifests as a
-- smaller sum. Using COLLATE "C" keeps the scalar comparable to non-vec mode.
select sum(length(min_url)) as sum_min_url_len,
       sum(length(min_title)) as sum_min_title_len,
       sum(grp_count) as sum_count,
       sum(grp_distinct) as sum_distinct
from (
    select grp,
           min(url COLLATE "C") as min_url,
           min(title COLLATE "C") as min_title,
           count(*) as grp_count,
           count(distinct uid) as grp_distinct
    from pax_test.like_slice_t
    where title like '%Google%'
    group by grp
) s;

drop table pax_test.like_slice_t;

reset vector.pool_threads;
reset vector.enable_vectorization;
