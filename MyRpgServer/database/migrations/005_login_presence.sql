USE account_db;

-- Adds presence timestamps to both fresh and converged legacy installations.
-- Dynamic statements keep this migration safe when legacy columns already exist.
SET @has_login_at = (
    SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'login_infos' AND column_name = 'login_at'
);
SET @sql = IF(@has_login_at = 0,
    'ALTER TABLE login_infos ADD COLUMN login_at DATETIME NULL AFTER create_at',
    'SELECT 1');
PREPARE migration_statement FROM @sql;
EXECUTE migration_statement;
DEALLOCATE PREPARE migration_statement;

SET @has_login_out = (
    SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'login_infos' AND column_name = 'login_out'
);
SET @sql = IF(@has_login_out = 0,
    'ALTER TABLE login_infos ADD COLUMN login_out DATETIME NULL AFTER login_at',
    'SELECT 1');
PREPARE migration_statement FROM @sql;
EXECUTE migration_statement;
DEALLOCATE PREPARE migration_statement;

ALTER TABLE login_infos
    MODIFY COLUMN is_active BOOLEAN NOT NULL DEFAULT FALSE,
    MODIFY COLUMN login_at DATETIME NULL DEFAULT NULL,
    MODIFY COLUMN login_out DATETIME NULL DEFAULT NULL;

-- A process restart invalidates connection-presence left by an ungraceful stop.
UPDATE login_infos
SET is_active = FALSE,
    login_out = NOW()
WHERE is_active = TRUE;

-- token_hash is already unique, so the old index duplicates the token lookup
-- path while increasing every insert/update cost.
SET @has_redundant_index = (
    SELECT COUNT(*) FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'login_infos'
      AND index_name = 'idx_login_infos_active_token_hash'
);
SET @sql = IF(@has_redundant_index > 0,
    'DROP INDEX idx_login_infos_active_token_hash ON login_infos',
    'SELECT 1');
PREPARE migration_statement FROM @sql;
EXECUTE migration_statement;
DEALLOCATE PREPARE migration_statement;
