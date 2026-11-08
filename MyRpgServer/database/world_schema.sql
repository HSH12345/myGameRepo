-- Additive installation; existing account/character schemas are never modified.
CREATE DATABASE IF NOT EXISTS world_db CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE world_db;
CREATE TABLE IF NOT EXISTS world_schema_version (
    id TINYINT UNSIGNED NOT NULL PRIMARY KEY,
    version INT UNSIGNED NOT NULL
) ENGINE=InnoDB;
INSERT IGNORE INTO world_schema_version(id,version) VALUES(1,1);
CREATE TABLE IF NOT EXISTS world_servers (
    game_port SMALLINT UNSIGNED NOT NULL PRIMARY KEY,
    boot_id BIGINT UNSIGNED NOT NULL,
    lease_until DATETIME(6) NOT NULL
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS world_maps (
    game_port SMALLINT UNSIGNED NOT NULL,
    instance_id BIGINT UNSIGNED NOT NULL,
    map_id INT UNSIGNED NOT NULL,
    map_version INT UNSIGNED NOT NULL,
    map_checksum BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY(game_port,instance_id),
    CONSTRAINT fk_world_map_server FOREIGN KEY(game_port) REFERENCES world_servers(game_port)
) ENGINE=InnoDB;
