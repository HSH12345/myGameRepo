-- Additive character UI preferences. No inventory/skill data is rewritten.
CREATE TABLE IF NOT EXISTS character_action_bar (
 character_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 skill_1 TINYINT UNSIGNED NOT NULL DEFAULT 3,
 skill_2 TINYINT UNSIGNED NOT NULL DEFAULT 4,
 skill_3 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_4 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_5 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_6 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_7 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_8 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_9 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 skill_10 TINYINT UNSIGNED NOT NULL DEFAULT 0,
 hp_potion INT UNSIGNED NOT NULL DEFAULT 0,
 mp_potion INT UNSIGNED NOT NULL DEFAULT 0,
 CONSTRAINT fk_action_bar_character FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
 CONSTRAINT ck_action_bar_skills CHECK(skill_1<=64 AND skill_2<=64 AND skill_3<=64 AND skill_4<=64 AND skill_5<=64 AND skill_6<=64 AND skill_7<=64 AND skill_8<=64 AND skill_9<=64 AND skill_10<=64),
 CONSTRAINT ck_action_bar_potions CHECK(hp_potion<=2147483647 AND mp_potion<=2147483647)
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version,checksum) VALUES(5,SHA2('character_action_bar_v5',256));
