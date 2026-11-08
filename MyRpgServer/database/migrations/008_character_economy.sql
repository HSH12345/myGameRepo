-- Run against the selected character schema. Does not drop/overwrite existing data.
CREATE TABLE IF NOT EXISTS character_economy (
 character_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 gold BIGINT UNSIGNED NOT NULL,
 economy_revision BIGINT UNSIGNED NOT NULL,
 session_fence BIGINT UNSIGNED NOT NULL,
 next_instance_id BIGINT UNSIGNED NOT NULL,
 CONSTRAINT fk_economy_character FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
 CONSTRAINT ck_economy_gold CHECK(gold<=1000000000000)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS character_items (
 character_id BIGINT UNSIGNED NOT NULL,
 instance_id BIGINT UNSIGNED NOT NULL,
 template_id INT UNSIGNED NOT NULL,
 quantity INT UNSIGNED NOT NULL,
 bag_slot TINYINT UNSIGNED NOT NULL,
 PRIMARY KEY(character_id,instance_id),
 UNIQUE KEY uq_character_bag(character_id,bag_slot),
 CONSTRAINT fk_item_character FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
 CONSTRAINT ck_item_quantity CHECK(quantity BETWEEN 1 AND 999),
 CONSTRAINT ck_item_bag CHECK(bag_slot<32)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS character_equipped_items (
 character_id BIGINT UNSIGNED NOT NULL,
 equipment_slot TINYINT UNSIGNED NOT NULL,
 instance_id BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(character_id,equipment_slot),
 UNIQUE KEY uq_equipped_instance(character_id,instance_id),
 CONSTRAINT fk_equipped_owner FOREIGN KEY(character_id,instance_id) REFERENCES character_items(character_id,instance_id) ON DELETE CASCADE,
 CONSTRAINT ck_equipment_slot CHECK(equipment_slot IN (0,1))
) ENGINE=InnoDB;
-- Durable deduplication. No TTL cleanup. Acquisition requests also use this journal.
CREATE TABLE IF NOT EXISTS character_economy_operations (
 character_id BIGINT UNSIGNED NOT NULL,
 operation_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 request_hash BINARY(32) NOT NULL,
 action INT UNSIGNED NOT NULL,
 session_fence BIGINT UNSIGNED NOT NULL,
 result INT UNSIGNED NOT NULL,
 result_revision BIGINT UNSIGNED NOT NULL,
 created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
 PRIMARY KEY(character_id,operation_id),
 CONSTRAINT fk_operation_character FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version,checksum) VALUES(3,SHA2('character_economy_v3',256));
