USE character_db;

-- Character-roster coordination, not account authentication data.
CREATE TABLE IF NOT EXISTS character_rosters (
    account_uid VARCHAR(128) NOT NULL PRIMARY KEY
) ENGINE=InnoDB;

-- Retain records for the lifetime of the account: expiring them would permit
-- an old creation operation to execute again. Account deletion must clean up.
CREATE TABLE IF NOT EXISTS character_creation_operations (
    account_uid VARCHAR(128) NOT NULL,
    operation_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    request_hash BINARY(32) NOT NULL,
    response_blob VARBINARY(4096) NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (account_uid, operation_id)
) ENGINE=InnoDB;

INSERT IGNORE INTO schema_migrations(version, checksum)
VALUES(2, SHA2('character_creation_v2', 256));
