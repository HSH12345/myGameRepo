USE account_db;

-- Convert existing plaintext login tokens to fixed-size SHA-256 lookup keys.
ALTER TABLE login_infos ADD COLUMN token_hash BINARY(32) NULL AFTER uid;
UPDATE login_infos SET token_hash = UNHEX(SHA2(token, 256));
ALTER TABLE login_infos MODIFY COLUMN token_hash BINARY(32) NOT NULL;
ALTER TABLE login_infos DROP INDEX uq_login_infos_token;
ALTER TABLE login_infos DROP INDEX idx_login_infos_active_token;
ALTER TABLE login_infos DROP COLUMN token;
ALTER TABLE login_infos ADD UNIQUE KEY uq_login_infos_token_hash (token_hash);
ALTER TABLE login_infos ADD KEY idx_login_infos_active_token_hash (is_active, token_hash);
