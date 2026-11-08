USE account_db;

-- Firebase custom/ID tokens are ASCII JWT values and can exceed the legacy 255-byte column.
-- ASCII keeps the indexed key below InnoDB's 3072-byte limit while preserving exact matching.
ALTER TABLE login_infos
    MODIFY COLUMN token VARCHAR(2048) CHARACTER SET ascii COLLATE ascii_bin NOT NULL;
