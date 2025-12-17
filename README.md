## Usage Instructions

Client
```bash
$ cd devices/client
$ make
$ ./app <ns-ip> <ns-client-port>
```

Name Server
```bash
$ cd devices/nameserver
$ make
$ ./bin/ns <ss-listener-port> <client-listner-port>
```

Storage Server
```bash
$ cd devices/storageserver
$ make
$ ./bin/ss <storage_path> <ss-port> <ns-ip> <ns-port>  
```

## General System Implementation

#### Components:
- **Client** (user interface)  
  Connects to NameServer to authenticate and locate files, and to StorageServer for sentence-level editing, reading, and streaming of files.

- **NameServer** (coordination service)  
  Tracks file metadata, user access, and storage server assignments. Handles user authentication, file command routing, and centralizes access control.

- **StorageServer** (distributed file & sentence store)  
  Stores actual files and performs sentence-level operations, including word/sentence splits, multi-user locking, versioned backups, and persistence.

#### Workflow:
- Users connect via the **Client**, which communicates with the **NameServer** to discover storage locations and routes file operations.
- **NameServer** keeps global mapping of which storage server owns each file, user access lists, and manages global sentence locks.
- **StorageServers** execute fine-level file operations (read, write, split, etc.), maintain backups, track locks locally, and update metadata.

---

## File-by-File Summary

### Top Level
- `README.md`: Overview and instructions (this file).
- `Makefile`: Top-level build orchestration.

---

### `/devices/client/`
- `src/main.c`: The main client program. Handles command-line interface, user authentication, and parsing commands (create/read/write/delete/list/access/stream). Connects to NameServer and StorageServer as needed.
- `include/client.h`: Client structures and function prototypes (Client struct, command handlers, connect/send/receive logic).

---

### `/devices/nameserver/`
- `src/main.c`: Main program for name server. Initializes/terminates the name server, launches connection threads, logs events.
- `src/access_control.c`: Manages file/user access control lists and access update logic.
- `src/acl_persistence.c`: Loads/saves ACLs from disk to preserve permissions.
- `src/client_sessions.c`: Handles user clients’ sessions (authentication, command routing, management).
- `src/hashtable.c`: Hash table implementation for mapping files to storage servers.
- `src/network.c`: Networking code for handling sockets, connections, events.
- `src/session_commands.c`: Handles user-initiated file operations and routes them accordingly.
- `src/ss_network.c`, `src/ss_sessions.c`: Handle StorageServer registration and session management.
- `src/storage_server_mgmt.c`: Functions for tracking/allocating storage servers, failover, and monitoring.
- `include/nameserver.h`: All core structures (session, file mapping, access, locks, config) and function APIs.

---

### `/devices/storageserver/`
- `src/main.c`: Main program for a storage server. Runs accept loop for clients, registers with the NameServer, runs storage logic on requests, and performs backup/recovery as needed.
- `src/metadata_ops.c`: Reads/writes/updates metadata for files (sentence/word/char counts, access times, etc.).
- `src/sentence_ops_multiword.c`: **Core logic for sentence- and word-level operations, including:**  
  - Loading files as lists of sentences and words  
  - Fine-grained per-sentence locks  
  - Sentence parsing/splitting on `.`, `?`, `!`  
  - Modifying, splitting, moving, and joining sentences/words via client commands.
  - Handles complex tail-split and move-on-edit behavior.
- `src/storage_ops.c`: Functions for file creation, reading, writing, backup, and deletion.
- `include/storageserver.h`: Main data structures for sentences, words, storage config, export of main operation functions.
- `storage_data1/`, `storage_data2/`: Subdirectories—physically store the actual file data and their metadata for each StorageServer instance.

---

### `/devices/common/`
- `common.h`: Project-wide constants, typedefs, protocol codes, error codes, utility macros, inline utilities (delimiter split, error handling, trimming, etc.).
- `include/`: Any cross-service headers needed.

---

## **How It All Works**
- **Sentence structure**: Each file is loaded, parsed into sentence nodes (split on `.`, `?`, `!`), each holding a doubly-linked word list.
- **Sentence/word edits**: Clients specify sentence and word indices and provide content. Modifications are split and routed in a way that preserves sentence boundaries.  
  - Complex rules ensure remaining words are moved when you split a sentence with a delimiter.
- **Locks**: Sentences can be locked for editing by users. NameServer tracks global sentence locks.
- **Backups**: On every write, backups are taken to allow `UNDO`.
- **Access control**: File permissions are centrally managed and updated through the NameServer.
- **Networking**: Uses Unix sockets, pthreads for concurrency, and a simple protocol for client-server interaction (`VIEW`, `CREATE`, `WRITE`, `UNDO`, `STREAM`, etc.).
- **Fault tolerance**: Both StorageServer and NameServer can recover from disconnects, using backups and persistent ACL/metadata.

---

## Short Example Workflow
1. Start NameServer and one or more StorageServers.
2. Users start Client, connect to NameServer, authenticate.
3. User creates a file, which is mapped by the NameServer to a StorageServer.
4. User writes, reads, streams, or manages access to the file.  
   - Every command is routed through NameServer, then (for data) to the right StorageServer.
   - Sentence-level writes use fine-grained locking and sentence boundary detection.
5. Changes are versioned (backups), can be undone, and all operations use proper access control.

