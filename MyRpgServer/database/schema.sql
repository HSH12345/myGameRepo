CREATE DATABASE IF NOT EXISTS account_db
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE account_db;

CREATE TABLE IF NOT EXISTS login_infos
(
    uid        VARCHAR(128) NOT NULL,
	 token_hash BINARY(32)   NOT NULL,
    is_active  BOOLEAN      NOT NULL DEFAULT FALSE,
    create_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
	 login_at   DATETIME     NULL,
	 login_out  DATETIME     NULL,
	 expires_at DATETIME     NOT NULL DEFAULT (CURRENT_TIMESTAMP + INTERVAL 30 DAY),
    PRIMARY KEY (uid),
	 UNIQUE KEY uq_login_infos_token_hash (token_hash)
) ENGINE = InnoDB;

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
