USE account_db;

-- One-time migration for the original login_infos schema (id/uid/token).
-- It preserves existing sessions while removing plaintext tokens after every
-- row has been converted successfully. Apply only after confirming that the
-- token_hash and expires_at columns do not already exist.
ALTER TABLE login_infos
    ADD COLUMN token_hash BINARY(32) NULL AFTER uid;

UPDATE login_infos
SET token_hash = UNHEX(SHA2(token, 256))
WHERE token_hash IS NULL AND token IS NOT NULL AND token <> '';

ALTER TABLE login_infos
    MODIFY COLUMN uid VARCHAR(128) NOT NULL,
    MODIFY COLUMN token_hash BINARY(32) NOT NULL,
    MODIFY COLUMN is_active BOOLEAN NOT NULL DEFAULT TRUE,
    ADD COLUMN expires_at DATETIME NULL AFTER create_at;

UPDATE login_infos
SET expires_at = DATE_ADD(COALESCE(create_at, NOW()), INTERVAL 30 DAY)
WHERE expires_at IS NULL;

ALTER TABLE login_infos
    MODIFY COLUMN expires_at DATETIME NOT NULL
        DEFAULT (CURRENT_TIMESTAMP + INTERVAL 30 DAY);

DROP INDEX token ON login_infos;

CREATE UNIQUE INDEX uq_login_infos_token_hash
    ON login_infos(token_hash);
CREATE INDEX idx_login_infos_active_token_hash
    ON login_infos(is_active, token_hash, expires_at);

ALTER TABLE login_infos
    DROP COLUMN token;
