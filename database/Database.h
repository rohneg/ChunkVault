#ifndef CHUNKVAULT_DATABASE_H
#define CHUNKVAULT_DATABASE_H

#include <mysql.h>
#include <string>
#include <vector>

struct FileRecord {
    std::string fileId;
    std::string fileName;
    unsigned long long originalSize = 0;
    unsigned int chunkCount = 0;
    std::string mimeType;
    std::string fileSha256;
};

struct ChunkRecord {
    std::string chunkId;
    std::string fileId;
    unsigned int chunkNumber = 0;
    unsigned long long chunkSize = 0;
    std::string checksum;
    std::string mainLocation;
    std::string node1Location;
    std::string node2Location;
};

struct ActivityRecord {
    unsigned long long id = 0;
    std::string action;
    std::string details;
    std::string createdAt;
};

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect();
    bool isConnected() const { return connection != nullptr; }

    bool insertFile(const std::string& fileId, const std::string& fileName,
                    unsigned long long fileSize, unsigned int chunkCount,
                    const std::string& mimeType, const std::string& fileSha256);
    bool insertChunk(const std::string& chunkId, const std::string& fileId,
                     unsigned int chunkNumber, unsigned long long chunkSize,
                     const std::string& checksum, const std::string& mainLocation,
                     const std::string& node1Location, const std::string& node2Location);
    bool deleteFile(const std::string& fileId);
    bool synchronizeDeletedFiles(const std::string& storageDirectory);

    bool getFiles(std::vector<FileRecord>& files);
    bool getFile(const std::string& fileId, FileRecord& file);
    bool getChunks(std::vector<ChunkRecord>& chunks, const std::string& fileId = "");
    bool getActivities(std::vector<ActivityRecord>& activities);
    bool deleteActivity(unsigned long long activityId);
    bool logActivity(const std::string& action, const std::string& details);
    bool getSuccessfulRecoveryCount(unsigned long long& count);
    bool getDeletedFileCount(unsigned long long& count);

private:
    MYSQL* connection = nullptr;

    std::string escape(const std::string& value) const;
    bool query(const std::string& sql);
    bool ensureActivityTable();
    bool countLogs(const char* action, unsigned long long& count);
};

#endif