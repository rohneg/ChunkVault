# ChunkVault

ChunkVault is a C++17 file storage project that demonstrates operating system and database concepts through a command-line program and a website. The website communicates with a local C++ API, so storage, integrity, recovery, scheduling, and database operations use the project backend.

## How storage works

A file is divided into automatically or manually sized chunks. Each chunk is saved as a main copy and two replicas, and its SHA-256 checksum is stored with its metadata. File and chunk records are kept in MySQL; `storage/chunk_metadata.txt` provides a local metadata log. Copy health is determined by comparing files on disk with their stored checksums.

## Website modules

- Dashboard: Shows stored-file, chunk, physical-copy, healthy-copy, corrupted-copy, and recovery totals, along with recent files and activity.
- File Storage: Uploads one file per operation, applies automatic or manual chunking, lists stored files, and deletes one or multiple files with their chunks, replicas, metadata, and related activity records.
- OS Simulator: Each selected stored file becomes a process with arrival time 0 and CPU burst equal to its chunk count. FCFS, SJF, and Round Robin scheduling report completion, waiting, and turnaround times, CPU utilization, throughput, and a Gantt chart. Resource allocation and requests appear in a process-resource graph; users can detect deadlocks, terminate processes to release resources, and reset the simulation.
- Integrity & Recovery: Shows every file, chunk, and copy status. Users can view reconstructed file contents, verify copies, and restore missing or damaged copies from a healthy replica.
- Database & Logs: Displays MySQL file and chunk metadata and persisted activity records. Selected activity records can be deleted from the database.

## Code modules

- `src/main.cpp` — command-line storage and verification workflow; starts the web server when requested.
- `src/web_server.cpp` and `src/web_server.h` — local HTTP server, website asset delivery, and API operations for files, integrity, recovery, activity, and OS simulation.
- `src/os_simulator.cpp` and `src/os_simulator.h` — FCFS, SJF, and Round Robin calculations, scheduling statistics, resource actions, and deadlock-cycle detection.
- `database/Database.cpp` and `database/Database.h` — MySQL connection and operations for file, chunk, and activity records.
- `web/index.html` — page shell and navigation; `web/style.css` — responsive layout and visual styles; `web/script.js` — page rendering and API-connected interactions.
- `storage/` — stored chunk and replica folders plus the local metadata log; `input/` — sample files.

The project uses C++17, HTML, CSS, JavaScript, MySQL, POSIX sockets, and Apple CommonCrypto for SHA-256 checksums.

## Database setup

Create the MySQL database tables with:

```sh
mysql -u root -p < database/schema.sql
```

Set the database password before starting the server:

```sh
export CHUNKVAULT_DB_PASSWORD='your_mysql_password'
```

The connection defaults to host `localhost`, user `root`, and database `chunkvault`. Set `CHUNKVAULT_DB_HOST`, `CHUNKVAULT_DB_USER`, or `CHUNKVAULT_DB_NAME` to use different values. Keep passwords out of source files and GitHub.
