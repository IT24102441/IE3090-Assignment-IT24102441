# RemoteOps — Remote System Monitoring and Management Tool

> **IE3090 Network Programming Assignment (Part 1)**  
> A custom client-server remote system monitoring, management, and telemetry suite built entirely in C using the BSD Sockets API and POSIX threads (`pthreads`).

---

## 📋 Personalisation Details

| Parameter | Configuration Value |
| :--- | :--- |
| **Registration Number** | `IT24102441` |
| **Listening Port** | `9410` |
| **Session ID (SID)** | `1442` |
| **Authentication Token** | `OPS-2441` |
| **Source Files** | `agent_441.c`, `controller_441.c`, `Makefile_441` |
| **Log File Name** | `remoteops_IT24102441.log` |
| **Storage Path** | `./agentfiles/IT24102441/` |

---

## 📖 Project Overview

**RemoteOps** is a dual-channel remote system administration and telemetry framework developed for Linux environments. It implements:
- A **Multithreaded TCP Agent (Server)** capable of handling multiple concurrent controller connections.
- An interactive **TCP Controller (Client)** providing an administrative command-line interface.
- A **Secondary UDP Telemetry Channel** (Port `9411`) delivering periodic real-time system performance updates without interfering with command-and-control operations.

The entire system is implemented in standard C utilizing low-level BSD socket primitives, POSIX thread synchronization primitives (mutexes), and Linux system interfaces (`sysinfo`, standard I/O streams).

---

## ✨ Features Implemented

### 1. Mandatory Core Features
- **Multithreaded Client Handling (POSIX Threads):**
  - Uses detached worker threads (`pthread_create` / `pthread_detach`) to service concurrent controller sessions without blocking the main listener.
- **Token-Based Authentication:**
  - Enforces mandatory handshake verification (`AUTH OPS-2441`) before permitting any administrative commands.
  - Automatically tags valid sessions with Session ID `SID:1442`.
- **System Telemetry (`SYSINFO`):**
  - Extracts 1-minute load average, active memory usage (MB), and system uptime (seconds) via `sysinfo(2)`.
- **Process Listing (`LISTPROC`):**
  - Queries active top running process names on the host system.
- **Whitelisted Remote Command Execution (`EXEC <CMD>`):**
  - Executes strict, sanitized whitelisted system commands via piped processes (`popen`):
    - `DATE` — Current system date and time.
    - `UPTIME` — System uptime in pretty format.
    - `DISKFREE` — Available disk space on the root filesystem.
    - `HOSTNAME` — Host network node name.
    - `WHOAMI` — User identity under which the agent runs.
  - Rejects unauthorized commands with standard error codes.
- **Binary File Transfer (`PUT` & `GET`):**
  - **Uploads (`PUT`):** Client streams binary data to the agent, saved securely in `./agentfiles/IT24102441/`.
  - **Downloads (`GET`):** Client downloads files from the agent directory with exact byte-length validation.
- **Periodic UDP Telemetry Streaming (`MONITOR START` / `MONITOR STOP`):**
  - Spawns a dedicated background thread on the Agent to broadcast asynchronous CPU load and memory metrics to the Controller on UDP port `9411` every 5 seconds.
- **Thread-Safe Logging & Graceful Disconnect:**
  - Mutex-locked file logger writing timestamped connection, command, and disconnection events to `remoteops_IT24102441.log`.
  - Safe teardown of sockets and worker/monitor threads upon `QUIT` or client disconnect.

### 2. Optional Extension
- **Telemetry Snapshot Persistence:**
  - Automatically records timestamped snapshots of all `SYSINFO` and `LISTPROC` results to `./agentfiles/IT24102441/history.txt`.
  - The historical log file can be retrieved remotely by the Controller at any time using the `GET history.txt` command.

---

## 🛠️ Architecture & Communication Flow

```
+-------------------------------------------------------------+
|                      Controller (Client)                   |
|                        controller_441.c                     |
+-------------------------------------------------------------+
         |                                           ^
         | TCP (Port 9410)                           | UDP (Port 9411)
         | Commands, Auth, File I/O                  | Telemetry Streams
         v                                           |
+-------------------------------------------------------------+
|                        Agent (Server)                       |
|                         agent_441.c                         |
|                                                             |
|  +------------------+             +----------------------+  |
|  | TCP Worker Thread|             |  UDP Monitor Thread  |  |
|  | (Pthread Mutex)  |             |  (5s System Stream)  |  |
|  +------------------+             +----------------------+  |
|           |                                                 |
|           v                                                 |
|  [remoteops_IT24102441.log]  &  [./agentfiles/IT24102441/]   |
+-------------------------------------------------------------+
```

---

## 📜 Protocol & Command Reference

| Command | Format / Syntax | Expected Server Response |
| :--- | :--- | :--- |
| **Authentication** | `AUTH OPS-2441` | `OK AUTHENTICATED SID:1442` |
| **System Info** | `SYSINFO` | `OK SYSINFO <load> <mem_mb> <uptime_sec> SID:1442` |
| **Process List** | `LISTPROC` | `OK PROCS <proc1,proc2,...> SID:1442` |
| **Exec Command** | `EXEC <DATE\|UPTIME\|DISKFREE\|HOSTNAME\|WHOAMI>` | `OK EXEC_RESULT <output> SID:1442` |
| **Upload File** | `PUT <filename>` | `OK PUT <filename> SID:1442` |
| **Download File**| `GET <filename>` | `OK GET <file_size> SID:1442` followed by raw bytes |
| **Start Stream** | `MONITOR START` | `OK MONITOR STARTED SID:1442` |
| **Stop Stream**  | `MONITOR STOP` | `OK MONITOR STOPPED SID:1442` |
| **Terminate**    | `QUIT` | `OK BYE SID:1442` |

---

## 📂 Project Structure

```
IE3090-Assignment-IT24102441/
├── Makefile_441                 # Build automation Makefile
├── agent_441.c                  # Multithreaded TCP/UDP Server source code
├── controller_441.c             # TCP Client/Controller source code
├── remoteops_IT24102441.log     # Thread-safe server activity log
├── agentfiles/
│   └── IT24102441/              # Dedicated storage folder for PUT/GET and history
│       └── history.txt          # Persistent snapshot log (SYSINFO / LISTPROC)
└── README.md                    # Project documentation
```

---

## 💻 Build and Execution Instructions

### Prerequisites
- GCC Compiler (`gcc`)
- POSIX-compliant Linux environment
- POSIX Threads library (`-pthread`)

### 1. Compilation
Compile both the `agent` and `controller` binaries using the designated Makefile:
```bash
make -f Makefile_441
```

### 2. Running the Agent (Server)
Start the Agent server listener:
```bash
./agent
```
*The agent will initialize the storage directories `./agentfiles/IT24102441/` and listen for incoming connections on port `9410`.*

### 3. Running the Controller (Client)
In a separate terminal window, start the Controller client:
```bash
./controller
```
*The controller will automatically connect to `127.0.0.1:9410`, execute the authentication handshake (`AUTH OPS-2441`), and open the interactive `Controller>` prompt.*

### 4. Cleaning Build Artifacts
To remove compiled executables:
```bash
make -f Makefile_441 clean
```

---

## 📝 Example Session

```text
$ ./controller
Connected to Agent at 127.0.0.1:9410
Agent: OK AUTHENTICATED SID:1442

Controller> SYSINFO
Agent: OK SYSINFO 0.12 1420 86400 SID:1442

Controller> EXEC UPTIME
Agent: OK EXEC_RESULT up 1 day, 2 hours SID:1442

Controller> EXEC WHOAMI
Agent: OK EXEC_RESULT user SID:1442

Controller> LISTPROC
Agent: OK PROCS systemd,kthreadd,bash,agent,controller SID:1442

Controller> GET history.txt
Agent: OK GET 112 SID:1442
Download complete: history.txt (112 bytes)

Controller> QUIT
Agent: OK BYE SID:1442
```
