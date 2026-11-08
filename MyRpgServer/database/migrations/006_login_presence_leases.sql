USE account_db;

SET @had_presence_server_table = (
    SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'login_server_instances'
);

CREATE TABLE IF NOT EXISTS login_server_instances
(
    server_instance_id VARCHAR(96) NOT NULL,
    lease_expires_at   DATETIME    NOT NULL,
    PRIMARY KEY (server_instance_id),
    KEY ix_login_server_instances_lease (lease_expires_at)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS login_presence_owners
(
    server_instance_id VARCHAR(96)  NOT NULL,
    uid                VARCHAR(128) NOT NULL,
    PRIMARY KEY (server_instance_id, uid),
    KEY ix_login_presence_owners_uid (uid),
    CONSTRAINT fk_login_presence_server
        FOREIGN KEY (server_instance_id) REFERENCES login_server_instances(server_instance_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_login_presence_account
        FOREIGN KEY (uid) REFERENCES login_infos(uid)
        ON DELETE CASCADE
) ENGINE = InnoDB;

-- Run this convergence only when the lease model is installed for the first
-- time. Re-running the migration must not log out live multi-server sessions.
SET @sql = IF(@had_presence_server_table = 0,
    'UPDATE login_infos SET is_active=FALSE, login_out=IFNULL(login_out, NOW()) WHERE is_active=TRUE',
    'SELECT 1');
PREPARE migration_statement FROM @sql;
EXECUTE migration_statement;
DEALLOCATE PREPARE migration_statement;
