-- common_setup.sql
-- Base setup for all datalake_fdw tests
-- This file should be included at the beginning of all test SQL files

-- Create required extensions
CREATE EXTENSION IF NOT EXISTS datalake_fdw;
CREATE EXTENSION IF NOT EXISTS hive_connector;

-- gp_exttable_delimiter may not be available in all environments
DO $$ BEGIN
    CREATE EXTENSION IF NOT EXISTS gp_exttable_delimiter;
EXCEPTION WHEN OTHERS THEN
    -- Extension not available, skip silently
END $$;

-- Create foreign data wrapper if not already present
-- Note: CREATE FOREIGN DATA WRAPPER does not support IF NOT EXISTS syntax
DO $$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_foreign_data_wrapper WHERE fdwname = 'datalake_fdw') THEN
        EXECUTE 'CREATE FOREIGN DATA WRAPPER datalake_fdw
            HANDLER datalake_fdw_handler
            VALIDATOR datalake_fdw_validator
            OPTIONS (mpp_execute ''all segments'')';
    END IF;
END $$;

-- Set standard date style for consistent output
SET datestyle = ISO, MDY;

-- Create logging utility function
CREATE OR REPLACE FUNCTION test_log(message text)
RETURNS void AS $$
BEGIN
    RAISE NOTICE '[TEST] %', message;
END;
$$ LANGUAGE plpgsql;

-- Log setup completion
SELECT test_log('Common setup completed');
