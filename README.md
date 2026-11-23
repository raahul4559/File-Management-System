# Web-Based File Management System Using C

**Operating Systems Mini Project**  
College / University Academic Project

A complete, functional file manager that runs entirely on a **custom HTTP server written in pure C**.  
All file operations (create, read, write, delete, rename, list, search, upload, download) are performed using **POSIX system calls**.

---

## Features

- Create new files and folders
- Upload files from the browser
- List directory contents with size and modification time
- Preview text files
- Download files
- Rename and delete files/folders
- Search files by name (recursive)
- Storage usage indicator
- Modern dark UI inspired by Google Drive / Finder
- Path sanitization – prevents directory traversal (`../`)
- Runs only on `127.0.0.1` by default

---

## Project Structure

```
FileManagementSystem/
├── backend/
│   ├── server.c              # Custom HTTP server (sockets)
│   ├── file_operations.c     # All POSIX file system calls
│   └── file_operations.h
├── frontend/
│   ├── index.html
│   ├── style.css
│   └── script.js
├── storage/                  # Restricted root for all operations
├── Makefile
└── README.md
```

---

## Requirements (macOS)

- Apple Clang (`clang`)
- Make
- No Node.js, Python, PHP, or any other backend language required

```bash
# Install Command Line Tools if needed
xcode-select --install
```

---

## Build & Run

```bash
cd FileManagementSystem

# Compile
make

# Run the server
make run
# or
./filemanager
```

Open your browser and go to:

```
http://127.0.0.1:8080
```

Press `Ctrl+C` in the terminal to stop the server.

---

## How It Works (OS Concepts Demonstrated)

| OS Concept              | How it is used in this project                                      |
|-------------------------|---------------------------------------------------------------------|
| File Management         | create, open, read, write, close, unlink, rename                    |
| Directory Management    | mkdir, rmdir, opendir, readdir, closedir                            |
| File Descriptors        | Every `open()` returns an fd that is later `close()`d               |
| System Calls            | All file operations go through POSIX syscalls (not C library only)  |
| Path Resolution         | Custom sanitization + `realpath()` to prevent traversal             |
| Permissions             | Files created with `0644`, directories with `0755`                   |
| Resource Management     | Proper closing of sockets and file descriptors                      |
| I/O Operations          | Blocking `read()` / `write()` on sockets and files                  |

**Distinction:**
- `fopen` / `fread` → C library (buffered)
- `open` / `read` / `write` / `close` → actual system calls (what we use)

---

## API Endpoints (for understanding / viva)

| Method | Endpoint          | Description                     |
|--------|-------------------|---------------------------------|
| GET    | `/api/list?path=` | List directory                  |
| GET    | `/api/usage`      | Total storage used              |
| POST   | `/api/create`     | Create file or folder           |
| POST   | `/api/delete`     | Delete file or folder           |
| POST   | `/api/rename`     | Rename                          |
| GET    | `/api/read?path=` | Read text file content          |
| GET    | `/api/search?q=`  | Search by name                  |
| POST   | `/api/upload`     | Upload (headers: X-Filename)    |
| GET    | `/api/download?path=` | Download file               |

---

## Security Measures

- All paths are sanitized; `..` and absolute paths are rejected
- Operations restricted to `./storage`
- Upload size limited to 10 MB
- Server binds only to `127.0.0.1`
- No execution of uploaded files
- Proper error handling and resource cleanup

---

## Demonstration Steps (for viva)

1. Start the server with `make run`
2. Open `http://127.0.0.1:8080`
3. Create a new folder → Create a new file inside it
4. Upload a text file from your computer
5. Preview the file
6. Rename a file
7. Search for a filename
8. Delete a file
9. Show storage usage changing
10. Explain the system calls used in `file_operations.c`

---

## Author

College student – Operating Systems Mini Project  
(Replace with your name, roll number, and college)

---

## License

For academic use only.
