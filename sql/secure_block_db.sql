-- Secure MySQL bootstrap for OmniCon block classes.
-- Run as an administrative MySQL user once MySQL server is available.

-- 1) Create database with modern charset/collation.
CREATE DATABASE IF NOT EXISTS omnicon_blocks
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

USE omnicon_blocks;

-- 2) Core table mapped from class `block` in block.hpp.
CREATE TABLE IF NOT EXISTS blocks (
  block_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  game_id INT NOT NULL,
  name VARCHAR(120) NOT NULL,
  game LONGBLOB NOT NULL,
  save_data LONGBLOB NULL,
  image LONGBLOB NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (block_id),
  UNIQUE KEY uq_blocks_game_id (game_id),
  UNIQUE KEY uq_blocks_game_name (game_id, name),
  KEY idx_blocks_game_id (game_id),
  CONSTRAINT chk_blocks_name_nonempty CHECK (CHAR_LENGTH(TRIM(name)) > 0)
) ENGINE=InnoDB;

ALTER TABLE blocks
  ADD COLUMN IF NOT EXISTS game LONGBLOB NOT NULL,
  ADD COLUMN IF NOT EXISTS save_data LONGBLOB NULL,
  ADD COLUMN IF NOT EXISTS image LONGBLOB NULL;

-- 3) Least-privilege app account for local-only access.
-- Replace CHANGE_ME_STRONG_PASSWORD before execution.
CREATE USER IF NOT EXISTS 'block_app'@'localhost' IDENTIFIED BY 'CHANGE_ME_STRONG_PASSWORD'
  REQUIRE SSL
  PASSWORD EXPIRE INTERVAL 180 DAY
  FAILED_LOGIN_ATTEMPTS 5
  PASSWORD_LOCK_TIME 2;

GRANT SELECT, INSERT, UPDATE, DELETE
ON omnicon_blocks.*
TO 'block_app'@'localhost';

FLUSH PRIVILEGES;
