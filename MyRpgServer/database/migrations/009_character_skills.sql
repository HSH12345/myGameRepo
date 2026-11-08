-- Additive character-schema migration. Existing economy/inventory and operation journal are preserved.
CREATE TABLE IF NOT EXISTS character_skill_progression (
 character_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 spent_points INT UNSIGNED NOT NULL DEFAULT 0,
 CONSTRAINT fk_skill_progression_character FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
 CONSTRAINT ck_skill_spent CHECK(spent_points<=1000000)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS character_skill_ranks (
 character_id BIGINT UNSIGNED NOT NULL,
 ability_id TINYINT UNSIGNED NOT NULL,
 skill_rank TINYINT UNSIGNED NOT NULL,
 PRIMARY KEY(character_id,ability_id),
 CONSTRAINT fk_skill_rank_character FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
 CONSTRAINT ck_skill_id CHECK(ability_id BETWEEN 3 AND 64),
 CONSTRAINT ck_skill_rank CHECK(skill_rank BETWEEN 1 AND 5)
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version,checksum) VALUES(4,SHA2('character_skills_v4',256));
