CREATE DATABASE IF NOT EXISTS character_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE character_db;

CREATE TABLE IF NOT EXISTS characters
(
    character_id   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    account_uid    VARCHAR(128) NOT NULL,
    character_name VARCHAR(24) NOT NULL,
    race           TINYINT UNSIGNED NOT NULL,
    gender         TINYINT UNSIGNED NOT NULL,
    level          INT UNSIGNED NOT NULL DEFAULT 1,
    version        BIGINT UNSIGNED NOT NULL DEFAULT 1,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at     DATETIME NULL,
    PRIMARY KEY (character_id),
    UNIQUE KEY uq_characters_name (character_name),
    KEY ix_characters_account (account_uid, deleted_at, character_id),
    CONSTRAINT ck_characters_level CHECK (level BETWEEN 1 AND 1000)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS character_equipment
(
    character_id    BIGINT UNSIGNED NOT NULL,
    equipment_slot  TINYINT UNSIGNED NOT NULL,
    item_template_id INT UNSIGNED NOT NULL,
    appearance_id   INT UNSIGNED NOT NULL DEFAULT 0,
    updated_at      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (character_id, equipment_slot),
    CONSTRAINT fk_character_equipment_character FOREIGN KEY (character_id)
        REFERENCES characters(character_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS schema_migrations
(
    version INT NOT NULL PRIMARY KEY,
    checksum CHAR(64) NOT NULL,
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

INSERT IGNORE INTO schema_migrations(version, checksum)
VALUES(1, SHA2('character_schema_v1', 256));

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

-- To enable economy and skill progression, also apply migrations in order:
-- 008_character_economy.sql, 009_character_skills.sql, then 010_character_action_bar.sql.
