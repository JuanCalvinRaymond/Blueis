# In-Memory Key-Value Database Server

This project is a high-performance, in-memory key-value store designed for low-latency reads and writes. It is structured as a client-server application communicating over TCP/IP, and is optimized for concurrent access, event-driven processing, and advanced querying capabilities.

## Features

- **Key-Value Storage with Hashmap**  
  Data is stored using a hashmap structure, utilizing the [Fowler–Noll–Vo (FNV)](https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function) hash function for fast and reliable key lookup.

- **Sorted Index with AVL Tree**  
  Supports efficient range queries and min/max retrieval using an AVL tree for ordered indexing.

- **Rank Queries with `zquery`**  
  Query the nth key-value pair based on sorted order for fast ranked access.

- **Event-Driven Networking**  
  Built on a non-blocking I/O architecture using file descriptors and an event loop to handle multiple clients simultaneously.

- **Thread-Safe Operations**  
  Utilizes a thread pool and mutex locking to ensure safe concurrent access and updates across multiple clients.

- **Idle Connection Management**  
  Actively monitors idle connections and terminates them after a configurable timeout to conserve server resources.

# How to
## Prerequisite
- g++
- cmake
- C++ 17

## Build Server
`cmake --build /mnt/g/GitHub/Blueis/build --config Debug --target Server -j 24 --`

## Build Client
`cmake --build /mnt/g/GitHub/Blueis/build --config Debug --target Server -j 24 --`

## Run Server
`./build/server`

## Run client and pass argument:
### Add entry to table:
`./client set hello world`

### Get entry from table:
`./client get hello`

### Delete entry from table:
`./client del hello`

### Display all key from table:
`./client keys`

### Add first entry to sorted set
`./client zadd zset 100.0f John`

### Query entry from sorted set
`./client zscore zset John`

### Add second entry
`./client zadd zset 200.0f Michael`

### Update second entry
`./client zadd zset 50.0f Michael`

### Rank Query
`./client zquery zset 50.0f "" 0.0 10.0`
`./client zquery zset 50.0f "" 1.0 10.0`

### Removes entries
`./client zrem zset John`
`./client zrem zset Michael`
