/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 *
 * Portions Copyright (c) 2025-2026, HashData Technology Limited.
 *
 * Simplified: TSDB uses load_external_function to bridge between a
 * "loader" .so and the main extension .so. We are a single .so, so
 * worker slot management uses a simple static counter instead.
 */
#ifndef BGW_LAUNCHER_INTERFACE_H
#define BGW_LAUNCHER_INTERFACE_H

#include <postgres.h>

extern bool ts_bgw_worker_reserve(void);
extern void ts_bgw_worker_release(void);

#endif /* BGW_LAUNCHER_INTERFACE_H */
