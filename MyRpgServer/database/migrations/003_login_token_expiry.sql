USE account_db;

ALTER TABLE login_infos
    ADD COLUMN expires_at DATETIME NOT NULL DEFAULT (CURRENT_TIMESTAMP + INTERVAL 30 DAY) AFTER create_at;

DROP INDEX idx_login_infos_active_token_hash ON login_infos;
CREATE INDEX idx_login_infos_active_token_hash
    ON login_infos(is_active, token_hash, expires_at);
