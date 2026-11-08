CREATE TABLE IF NOT EXISTS schema_migrations
(
    version    INT      NOT NULL PRIMARY KEY,
    checksum   CHAR(64) NOT NULL,
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE = InnoDB;

INSERT IGNORE INTO schema_migrations(version, checksum)
VALUES (7, SHA2('007_schema_version_baseline', 256));
