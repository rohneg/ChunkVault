
#include "Database.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

Database::Database() : connection(nullptr) {}

Database::~Database() {
    if (connection != nullptr) {
        mysql_close(connection);
    }
}

bool Database::connect() {
    const char* host = std::getenv("CHUNKVAULT_DB_HOST");
    const char* user = std::getenv("CHUNKVAULT_DB_USER");
    const char* password = std::getenv("CHUNKVAULT_DB_PASSWORD");
    const char* database = std::getenv("CHUNKVAULT_DB_NAME");

    if (host == nullptr) host = "localhost";
    if (user == nullptr) user = "root";
    if (database == nullptr) database = "chunkvault";

    if (password == nullptr) {
        std::cerr << "CHUNKVAULT_DB_PASSWORD is not set.\n";
        return false;
    }

    MYSQL* handle = mysql_init(nullptr);
    if (handle == nullptr) {
        std::cerr << "MySQL initialization failed.\n";
        return false;
    }

    MYSQL* connected = mysql_real_connect(
        handle,
        host,
        user,
        password,
        database,
        0,
        nullptr,
        0
    );

    if (connected == nullptr) {
        std::cerr << "MySQL connection failed: " << mysql_error(handle) << '\n';
        mysql_close(handle);
        return false;
    }

    connection = connected;
    return true;
}

bool Database::isConnected() const {
    return connection != nullptr;
}

bool Database::insertFile(
    const std::string& fileId,
    const std::string& fileName,
    unsigned long long fileSize,
    unsigned int chunkCount
) {
    if (!isConnected()) {
        return false;
    }

    MYSQL_STMT* statement = mysql_stmt_init(connection);
    if (statement == nullptr) {
        return false;
    }

    const char* query =
        "INSERT INTO files (file_id, file_name, original_size, chunk_count) "
        "VALUES (?, ?, ?, ?)";

    if (mysql_stmt_prepare(statement, query, strlen(query)) != 0) {
        mysql_stmt_close(statement);
        return false;
    }

    MYSQL_BIND parameters[4]{};
    unsigned long fileNameLength = fileName.size();

    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = const_cast<char*>(fileId.c_str());
    parameters[0].buffer_length = fileId.size();

    parameters[1].buffer_type = MYSQL_TYPE_STRING;
    parameters[1].buffer = const_cast<char*>(fileName.c_str());
    parameters[1].buffer_length = fileName.size();
    parameters[1].length = &fileNameLength;

    parameters[2].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[2].buffer = &fileSize;
    parameters[2].is_unsigned = true;

    parameters[3].buffer_type = MYSQL_TYPE_LONG;
    parameters[3].buffer = &chunkCount;
    parameters[3].is_unsigned = true;

    bool success =
        mysql_stmt_bind_param(statement, parameters) == 0 &&
        mysql_stmt_execute(statement) == 0;

    mysql_stmt_close(statement);
    return success;
}

bool Database::insertChunk(
    const std::string& chunkId,
    const std::string& fileId,
    unsigned int chunkNumber,
    unsigned long long chunkSize,
    const std::string& checksum,
    const std::string& mainLocation,
    const std::string& node1Location,
    const std::string& node2Location
) {
    if (!isConnected()) {
        return false;
    }

    MYSQL_STMT* statement = mysql_stmt_init(connection);
    if (statement == nullptr) {
        return false;
    }

    const char* query =
        "INSERT INTO chunks "
        "(chunk_id, file_id, chunk_number, chunk_size, checksum, "
        "main_location, node1_location, node2_location) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(statement, query, strlen(query)) != 0) {
        mysql_stmt_close(statement);
        return false;
    }

    MYSQL_BIND parameters[8]{};
    unsigned long lengths[8] = {
        static_cast<unsigned long>(chunkId.size()),
        static_cast<unsigned long>(fileId.size()),
        0,
        0,
        static_cast<unsigned long>(checksum.size()),
        static_cast<unsigned long>(mainLocation.size()),
        static_cast<unsigned long>(node1Location.size()),
        static_cast<unsigned long>(node2Location.size())
    };

    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = const_cast<char*>(chunkId.c_str());
    parameters[0].buffer_length = lengths[0];
    parameters[0].length = &lengths[0];

    parameters[1].buffer_type = MYSQL_TYPE_STRING;
    parameters[1].buffer = const_cast<char*>(fileId.c_str());
    parameters[1].buffer_length = lengths[1];
    parameters[1].length = &lengths[1];

    parameters[2].buffer_type = MYSQL_TYPE_LONG;
    parameters[2].buffer = &chunkNumber;
    parameters[2].is_unsigned = true;

    parameters[3].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[3].buffer = &chunkSize;
    parameters[3].is_unsigned = true;

    parameters[4].buffer_type = MYSQL_TYPE_STRING;
    parameters[4].buffer = const_cast<char*>(checksum.c_str());
    parameters[4].buffer_length = lengths[4];
    parameters[4].length = &lengths[4];

    parameters[5].buffer_type = MYSQL_TYPE_STRING;
    parameters[5].buffer = const_cast<char*>(mainLocation.c_str());
    parameters[5].buffer_length = lengths[5];
    parameters[5].length = &lengths[5];

    parameters[6].buffer_type = MYSQL_TYPE_STRING;
    parameters[6].buffer = const_cast<char*>(node1Location.c_str());
    parameters[6].buffer_length = lengths[6];
    parameters[6].length = &lengths[6];

    parameters[7].buffer_type = MYSQL_TYPE_STRING;
    parameters[7].buffer = const_cast<char*>(node2Location.c_str());
    parameters[7].buffer_length = lengths[7];
    parameters[7].length = &lengths[7];

    bool success =
        mysql_stmt_bind_param(statement, parameters) == 0 &&
        mysql_stmt_execute(statement) == 0;

    mysql_stmt_close(statement);
    return success;
}

bool Database::deleteFile(const std::string& fileId) {
    if (!isConnected()) {
        return false;
    }
    if (!ensureActivityTable() || mysql_query(connection, "START TRANSACTION") != 0) {
        return false;
    }

    std::string escapedId(fileId.size() * 2 + 1, '\0');
    unsigned long escapedLength = mysql_real_escape_string(
        connection, escapedId.data(), fileId.c_str(), static_cast<unsigned long>(fileId.size()));
    escapedId.resize(escapedLength);
    const std::string id = "'" + escapedId + "'";

    bool success = mysql_query(connection,
        ("DELETE FROM activity_logs WHERE "
         "(`action`='STORAGE' AND details LIKE '% (" + escapedId + ") stored with %') OR "
         "(`action`='VERIFY' AND details LIKE 'Verified " + escapedId + " (%') OR "
         "(`action` IN ('RECOVERY','RECOVERY_NOOP','SIMULATE_CORRUPTION','SIMULATE_MISSING') "
         "AND details LIKE '" + escapedId + ":%')").c_str()) == 0;
    success = success && mysql_query(connection,
        ("DELETE FROM chunks WHERE file_id=" + id).c_str()) == 0;
    success = success && mysql_query(connection,
        ("DELETE FROM files WHERE file_id=" + id).c_str()) == 0 &&
        mysql_affected_rows(connection) == 1;
    if (success) {
        const std::string details = fileId + " deleted; counted once as missing.";
        std::string escapedDetails(details.size() * 2 + 1, '\0');
        unsigned long detailsLength = mysql_real_escape_string(
            connection, escapedDetails.data(), details.c_str(), static_cast<unsigned long>(details.size()));
        escapedDetails.resize(detailsLength);
        success = mysql_query(connection,
            ("INSERT INTO activity_logs (`action`, details) VALUES ('FILE_DELETE','" +
             escapedDetails + "')").c_str()) == 0;
    }

    if (success) {
        success = mysql_query(connection, "COMMIT") == 0;
    }
    if (!success) mysql_query(connection, "ROLLBACK");
    return success;
}

bool Database::synchronizeDeletedFiles(
    const std::string& storageDirectory
) {
    namespace fs = std::filesystem;

    if (!isConnected() ||
        !fs::exists(storageDirectory) ||
        !fs::is_directory(storageDirectory)) {
        return false;
    }

    if (mysql_query(connection, "SELECT file_id, file_name FROM files") != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(connection);

    if (result == nullptr) {
        return false;
    }

    std::vector<std::string> deletedFileIds;
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(result)) != nullptr) {
        if (row[0] == nullptr || row[1] == nullptr) {
            continue;
        }

        std::string fileId = row[0];
        std::string fileName = row[1];

        fs::path fileDirectory =
            fs::path(storageDirectory) /
            (fileName + " (" + fileId + ")");

        if (!fs::exists(fileDirectory) ||
            !fs::is_directory(fileDirectory)) {
            deletedFileIds.push_back(fileId);
        }
    }

    mysql_free_result(result);

    for (const auto& fileId : deletedFileIds) {
        if (!deleteFile(fileId)) {
            return false;
        }
    }

    return true;
}

bool Database::getFiles(std::vector<FileRecord>& files) {
    files.clear();

    if (!isConnected() || mysql_query(
            connection,
            "SELECT file_id, file_name, original_size, chunk_count "
            "FROM files ORDER BY file_id") != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (result == nullptr) {
        return false;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        FileRecord file{};
        file.fileId = row[0] == nullptr ? "" : row[0];
        file.fileName = row[1] == nullptr ? "" : row[1];
        file.originalSize = row[2] == nullptr ? 0 : std::strtoull(row[2], nullptr, 10);
        file.chunkCount = row[3] == nullptr ? 0 : static_cast<unsigned int>(std::strtoul(row[3], nullptr, 10));
        files.push_back(std::move(file));
    }

    mysql_free_result(result);
    return true;
}

bool Database::getChunks(std::vector<ChunkRecord>& chunks, const std::string& fileId) {
    chunks.clear();

    if (!isConnected()) {
        return false;
    }

    std::string query =
        "SELECT chunk_id, file_id, chunk_number, chunk_size, checksum, "
        "main_location, node1_location, node2_location FROM chunks";

    if (!fileId.empty()) {
        char* escaped = new char[fileId.size() * 2 + 1];
        unsigned long escapedLength = mysql_real_escape_string(
            connection,
            escaped,
            fileId.c_str(),
            static_cast<unsigned long>(fileId.size())
        );
        escaped[escapedLength] = '\0';
        query += " WHERE file_id='" + std::string(escaped, escapedLength) + "'";
        delete[] escaped;
    }

    query += " ORDER BY file_id, chunk_number";

    if (mysql_query(connection, query.c_str()) != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (result == nullptr) {
        return false;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        ChunkRecord chunk{};
        chunk.chunkId = row[0] == nullptr ? "" : row[0];
        chunk.fileId = row[1] == nullptr ? "" : row[1];
        chunk.chunkNumber = row[2] == nullptr ? 0 : static_cast<unsigned int>(std::strtoul(row[2], nullptr, 10));
        chunk.chunkSize = row[3] == nullptr ? 0 : std::strtoull(row[3], nullptr, 10);
        chunk.checksum = row[4] == nullptr ? "" : row[4];
        chunk.mainLocation = row[5] == nullptr ? "" : row[5];
        chunk.node1Location = row[6] == nullptr ? "" : row[6];
        chunk.node2Location = row[7] == nullptr ? "" : row[7];
        chunks.push_back(std::move(chunk));
    }

    mysql_free_result(result);
    return true;
}

bool Database::ensureActivityTable() {
    if (!isConnected()) {
        return false;
    }

    return mysql_query(
        connection,
        "CREATE TABLE IF NOT EXISTS activity_logs ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, "
        "`action` VARCHAR(64) NOT NULL, "
        "details TEXT NOT NULL, "
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB"
    ) == 0;
}

bool Database::getActivities(std::vector<ActivityRecord>& activities) {
    activities.clear();

    if (!ensureActivityTable() || mysql_query(
            connection,
            "SELECT id, `action`, details, created_at FROM activity_logs "
            "ORDER BY id DESC LIMIT 100") != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (result == nullptr) {
        return false;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        ActivityRecord activity{};
        activity.id = row[0] == nullptr ? 0 : std::strtoull(row[0], nullptr, 10);
        activity.action = row[1] == nullptr ? "" : row[1];
        activity.details = row[2] == nullptr ? "" : row[2];
        activity.createdAt = row[3] == nullptr ? "" : row[3];
        activities.push_back(std::move(activity));
    }

    mysql_free_result(result);
    return true;
}

bool Database::deleteActivity(unsigned long long activityId) {
    if (!ensureActivityTable()) {
        return false;
    }

    const std::string query = "DELETE FROM activity_logs WHERE id=" + std::to_string(activityId);
    return mysql_query(connection, query.c_str()) == 0;
}

bool Database::logActivity(const std::string& action, const std::string& details) {
    if (!ensureActivityTable()) {
        return false;
    }

    char* escapedAction = new char[action.size() * 2 + 1];
    char* escapedDetails = new char[details.size() * 2 + 1];
    unsigned long actionLength = mysql_real_escape_string(
        connection,
        escapedAction,
        action.c_str(),
        static_cast<unsigned long>(action.size())
    );
    unsigned long detailsLength = mysql_real_escape_string(
        connection,
        escapedDetails,
        details.c_str(),
        static_cast<unsigned long>(details.size())
    );
    escapedAction[actionLength] = '\0';
    escapedDetails[detailsLength] = '\0';

    std::string query =
        "INSERT INTO activity_logs (`action`, details) VALUES ('" +
        std::string(escapedAction, actionLength) + "', '" +
        std::string(escapedDetails, detailsLength) + "')";

    delete[] escapedAction;
    delete[] escapedDetails;
    return mysql_query(connection, query.c_str()) == 0;
}

bool Database::getSuccessfulRecoveryCount(unsigned long long& count) {
    count = 0;

    if (!ensureActivityTable() || mysql_query(
            connection,
            "SELECT COUNT(*) FROM activity_logs WHERE `action`='RECOVERY'") != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (result == nullptr) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (row != nullptr && row[0] != nullptr) {
        count = std::strtoull(row[0], nullptr, 10);
    }
    mysql_free_result(result);
    return true;
}

bool Database::getDeletedFileCount(unsigned long long& count) {
    count = 0;
    if (!ensureActivityTable() || mysql_query(
            connection,
            "SELECT COUNT(*) FROM activity_logs WHERE `action`='FILE_DELETE'") != 0) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (result == nullptr) return false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row != nullptr && row[0] != nullptr) {
        count = std::strtoull(row[0], nullptr, 10);
    }
    mysql_free_result(result);
    return true;
}
