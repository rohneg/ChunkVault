## Current Implementation Status

ChunkVault is a C++-based chunk storage system developed to demonstrate Operating System and DBMS concepts.

### Implemented Features

* Input file validation
* Automatic and manual chunk-size selection
* Unique file and chunk identifiers
* Buffered binary file handling
* Chunk storage with replicas in two storage locations
* SHA-256 checksum generation
* Persistent chunk metadata
* Integrity verification of stored chunks and replicas
* Detection of modified or missing chunks

### Upcoming Phase 2 Work

* Multiple-file storage with isolated directories
* MySQL metadata integration
* FCFS and SJF scheduling for file-processing jobs
* Recovery of corrupted or missing chunks using healthy replicas
* Original-file reconstruction
* End-to-end testing and performance evaluation
