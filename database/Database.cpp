#include "Database.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace {

std::string text(const char* value) { return value ? value : ""; }
unsigned long long number(const char* value) { return value ? std::strtoull(value, nullptr, 10) : 0; }

const char* setting(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

const char* const kFileColumns = "file_id, file_name, original_size, chunk_count, mime_type, file_sha256";

FileRecord fileFrom(MYSQL_ROW row) {
    return {text(row[0]), text(row[1]), number(row[2]), static_cast<unsigned int>(number(row[3])),
            text(row[4]), text(row[5])};
}

template <class OnRow>
bool fetchRows(MYSQL* connection, const std::string& sql, OnRow onRow) {
    if (mysql_query(connection, sql.c_str()) != 0) return false;
    MYSQL_RES* result = mysql_store_result(connection);
    if (!result) return false;
    while (MYSQL_ROW row = mysql_fetch_row(result)) onRow(row);
    mysql_free_result(result);
    return true;
}

void bindText(MYSQL_BIND& bind, const std::string& value, unsigned long& length) {
    length = static_cast<unsigned long>(value.size());
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.c_str());
    bind.buffer_length = length;
    bind.length = &length;
}

void bindNumber(MYSQL_BIND& bind, void* value, enum_field_types type) {
    bind.buffer_type = type;
    bind.buffer = value;
    bind.is_unsigned = true;
}

bool execute(MYSQL* connection, const char* sql, MYSQL_BIND* parameters) {
    MYSQL_STMT* statement = mysql_stmt_init(connection);
    if (!statement) return false;
    const bool ok = mysql_stmt_prepare(statement, sql, std::strlen(sql)) == 0 &&
                    mysql_stmt_bind_param(statement, parameters) == 0 &&
                    mysql_stmt_execute(statement) == 0;
    mysql_stmt_close(statement);
    return ok;
}

}

Database::~Database() {
    if (connection) mysql_close(connection);
}

bool Database::connect() {
    if (connection) return true;
    const char* password = std::getenv("CHUNKVAULT_DB_PASSWORD");
    if (!password) {
        std::cerr << "CHUNKVAULT_DB_PASSWORD is not set.\n";
        return false;
    }
    MYSQL* handle = mysql_init(nullptr);
    if (!handle) {
        std::cerr << "MySQL initialization failed.\n";
        return false;
    }
    if (!mysql_real_connect(handle, setting("CHUNKVAULT_DB_HOST", "localhost"),
                            setting("CHUNKVAULT_DB_USER", "root"), password,
                            setting("CHUNKVAULT_DB_NAME", "chunkvault"), 0, nullptr, 0)) {
        std::cerr << "MySQL connection failed: " << mysql_error(handle) << '\n';
        mysql_close(handle);
        return false;
    }
    connection = handle;
    return true;
}

std::string Database::escape(const std::string& value) const {
    std::string out(value.size() * 2 + 1, '\0');
    out.resize(mysql_real_escape_string(connection, out.data(), value.c_str(),
                                        static_cast<unsigned long>(value.size())));
    return out;
}

bool Database::query(const std::string& sql) {
    return mysql_query(connection, sql.c_str()) == 0;
}

bool Database::insertFile(const std::string& fileId, const std::string& fileName,
                          unsigned long long fileSize, unsigned int chunkCount,
                          const std::string& mimeType, const std::string& fileSha256) {
    if (!isConnected()) return false;
    MYSQL_BIND p[6]{};
    unsigned long length[4];
    bindText(p[0], fileId, length[0]);
    bindText(p[1], fileName, length[1]);
    bindNumber(p[2], &fileSize, MYSQL_TYPE_LONGLONG);
    bindNumber(p[3], &chunkCount, MYSQL_TYPE_LONG);
    bindText(p[4], mimeType, length[2]);
    bindText(p[5], fileSha256, length[3]);
    return execute(connection,
        "INSERT INTO files (file_id, file_name, original_size, chunk_count, mime_type, file_sha256) "
        "VALUES (?, ?, ?, ?, ?, ?)", p);
}

bool Database::insertChunk(const std::string& chunkId, const std::string& fileId,
                           unsigned int chunkNumber, unsigned long long chunkSize,
                           const std::string& checksum, const std::string& mainLocation,
                           const std::string& node1Location, const std::string& node2Location) {
    if (!isConnected()) return false;
    MYSQL_BIND p[8]{};
    unsigned long length[6];
    bindText(p[0], chunkId, length[0]);
    bindText(p[1], fileId, length[1]);
    bindNumber(p[2], &chunkNumber, MYSQL_TYPE_LONG);
    bindNumber(p[3], &chunkSize, MYSQL_TYPE_LONGLONG);
    bindText(p[4], checksum, length[2]);
    bindText(p[5], mainLocation, length[3]);
    bindText(p[6], node1Location, length[4]);
    bindText(p[7], node2Location, length[5]);
    return execute(connection,
        "INSERT INTO chunks (chunk_id, file_id, chunk_number, chunk_size, checksum, "
        "main_location, node1_location, node2_location) VALUES (?, ?, ?, ?, ?, ?, ?, ?)", p);
}

bool Database::deleteFile(const std::string& fileId) {
    if (!ensureActivityTable() || !query("START TRANSACTION")) return false;

    const std::string id = escape(fileId);
    const bool ok =
        query("DELETE FROM activity_logs WHERE "
              "(`action`='STORAGE' AND details LIKE '% (" + id + ") stored with %') OR "
              "(`action`='VERIFY' AND details LIKE 'Verified " + id + " (%') OR "
              "(`action` IN ('RECOVERY','RECOVERY_NOOP','SIMULATE_CORRUPTION','SIMULATE_MISSING') "
              "AND details LIKE '" + id + ":%')") &&
        query("DELETE FROM chunks WHERE file_id='" + id + "'") &&
        query("DELETE FROM files WHERE file_id='" + id + "'") &&
        mysql_affected_rows(connection) == 1 &&
        query("INSERT INTO activity_logs (`action`, details) VALUES ('FILE_DELETE','" +
              escape(fileId + " deleted; counted once as missing.") + "')") &&
        query("COMMIT");

    if (!ok) query("ROLLBACK");
    return ok;
}

bool Database::synchronizeDeletedFiles(const std::string& storageDirectory) {
    namespace fs = std::filesystem;
    std::vector<FileRecord> files;
    if (!isConnected() || !fs::is_directory(storageDirectory) || !getFiles(files)) return false;

    for (const auto& file : files) {
        const fs::path directory = fs::path(storageDirectory) / (file.fileName + " (" + file.fileId + ")");
        if (!fs::is_directory(directory) && !deleteFile(file.fileId)) return false;
    }
    return true;
}

bool Database::getFiles(std::vector<FileRecord>& files) {
    files.clear();
    return isConnected() && fetchRows(connection,
        std::string("SELECT ") + kFileColumns + " FROM files ORDER BY file_id",
        [&](MYSQL_ROW row) { files.push_back(fileFrom(row)); });
}

bool Database::getFile(const std::string& fileId, FileRecord& file) {
    bool found = false;
    const bool ok = isConnected() && fetchRows(connection,
        std::string("SELECT ") + kFileColumns + " FROM files WHERE file_id='" + escape(fileId) + "'",
        [&](MYSQL_ROW row) { file = fileFrom(row); found = true; });
    return ok && found;
}

bool Database::getChunks(std::vector<ChunkRecord>& chunks, const std::string& fileId) {
    chunks.clear();
    if (!isConnected()) return false;

    std::string sql =
        "SELECT chunk_id, file_id, chunk_number, chunk_size, checksum, "
        "main_location, node1_location, node2_location FROM chunks";
    if (!fileId.empty()) sql += " WHERE file_id='" + escape(fileId) + "'";
    sql += " ORDER BY file_id, chunk_number";

    return fetchRows(connection, sql, [&](MYSQL_ROW row) {
        chunks.push_back({text(row[0]), text(row[1]), static_cast<unsigned int>(number(row[2])),
                          number(row[3]), text(row[4]), text(row[5]), text(row[6]), text(row[7])});
    });
}

bool Database::ensureActivityTable() {
    return isConnected() && query(
        "CREATE TABLE IF NOT EXISTS activity_logs ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, "
        "`action` VARCHAR(64) NOT NULL, "
        "details TEXT NOT NULL, "
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB");
}

bool Database::getActivities(std::vector<ActivityRecord>& activities) {
    activities.clear();
    return ensureActivityTable() && fetchRows(connection,
        "SELECT id, `action`, details, created_at FROM activity_logs ORDER BY id DESC LIMIT 100",
        [&](MYSQL_ROW row) {
            activities.push_back({number(row[0]), text(row[1]), text(row[2]), text(row[3])});
        });
}

bool Database::deleteActivity(unsigned long long activityId) {
    return ensureActivityTable() &&
           query("DELETE FROM activity_logs WHERE id=" + std::to_string(activityId));
}

bool Database::logActivity(const std::string& action, const std::string& details) {
    return ensureActivityTable() &&
           query("INSERT INTO activity_logs (`action`, details) VALUES ('" +
                 escape(action) + "', '" + escape(details) + "')");
}

bool Database::countLogs(const char* action, unsigned long long& count) {
    count = 0;
    return ensureActivityTable() && fetchRows(connection,
        std::string("SELECT COUNT(*) FROM activity_logs WHERE `action`='") + action + "'",
        [&](MYSQL_ROW row) { count = number(row[0]); });
}

bool Database::getSuccessfulRecoveryCount(unsigned long long& count) { return countLogs("RECOVERY", count); }
bool Database::getDeletedFileCount(unsigned long long& count) { return countLogs("FILE_DELETE", count); }