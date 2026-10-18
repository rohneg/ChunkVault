CREATE DATABASE IF NOT EXISTS chunkvault;
USE chunkvault;

CREATE TABLE IF NOT EXISTS files (
    file_id VARCHAR(32) NOT NULL PRIMARY KEY,
    file_name VARCHAR(255) NOT NULL,
    original_size BIGINT UNSIGNED NOT NULL,
    chunk_count INT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS chunks (
    chunk_id VARCHAR(32) NOT NULL PRIMARY KEY,
    file_id VARCHAR(32) NOT NULL,
    chunk_number INT UNSIGNED NOT NULL,
    chunk_size BIGINT UNSIGNED NOT NULL,
    checksum CHAR(64) NOT NULL,
    main_location TEXT NOT NULL,
    node1_location TEXT NOT NULL,
    node2_location TEXT NOT NULL,
    UNIQUE KEY unique_file_chunk (file_id, chunk_number),
    CONSTRAINT fk_chunks_file FOREIGN KEY (file_id)
        REFERENCES files (file_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS activity_logs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `action` VARCHAR(64) NOT NULL,
    details TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
