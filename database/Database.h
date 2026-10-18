
#ifndef CHUNKVAULT_DATABASE_H
#define CHUNKVAULT_DATABASE_H

#include <string>
#include <vector>
#include <mysql.h>

struct FileRecord {
    std::string fileId;
    std::string fileName;
    unsigned long long originalSize;
    unsigned int chunkCount;
};

struct ChunkRecord {
    std::string chunkId;
    std::string fileId;
    unsigned int chunkNumber;
    unsigned long long chunkSize;
    std::string checksum;
    std::string mainLocation;
    std::string node1Location;
    std::string node2Location;
};

struct ActivityRecord {
    unsigned long long id;
    std::string action;
    std::string details;
    std::string createdAt;
};

class Database {
private:
    MYSQL* connection;

public:
    Database();
    ~Database();

    bool connect();
    bool insertFile(const std::string& fileId,
                    const std::string& fileName,
                    unsigned long long fileSize,
                    unsigned int chunkCount);

    bool insertChunk(const std::string& chunkId,
                     const std::string& fileId,
                     unsigned int chunkNumber,
                     unsigned long long chunkSize,
                     const std::string& checksum,
                     const std::string& mainLocation,
                     const std::string& node1Location,
                     const std::string& node2Location);

    bool isConnected() const;
    bool deleteFile(const std::string& fileId);
    bool synchronizeDeletedFiles(const std::string& storageDirectory);
    bool getFiles(std::vector<FileRecord>& files);
    bool getChunks(std::vector<ChunkRecord>& chunks, const std::string& fileId = "");
    bool getActivities(std::vector<ActivityRecord>& activities);
    bool deleteActivity(unsigned long long activityId);
    bool logActivity(const std::string& action, const std::string& details);
    bool getSuccessfulRecoveryCount(unsigned long long& count);
    bool getDeletedFileCount(unsigned long long& count);

private:
    bool ensureActivityTable();
};

#endif
