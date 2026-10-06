# Design Diary: RemoteOps Implementation

**Student ID:** IT24102213 
**Student Name:** P. Arachchige Adeesa Induranga 
**Module:** IE3090 – Network Programming 

---

### Stage 1: Planning and Architecture Design (Commits 1–3)
* **Goal:** Set up project scaffolding, build system, and establish concurrent network socket infrastructure.
* **Key Decisions:**
  * Adopted standard POSIX Threads (`pthreads`) rather than `fork()` or `select()`. Threads provide shared memory state, lower context-switching overhead, and simpler per-client lifecycle management.
  * Formulated personalized parameters based on student ID `IT24102213`: Port `9410`, Session ID `SID:3122`, Auth Token `OPS-2213`.
* **Challenges & Solutions:**
  * Encountered `EADDRINUSE` errors during immediate server restarts. Resolved by setting the `SO_REUSEADDR` socket option on the listening descriptor before binding.

---

### Stage 2: Handshake, Authentication, and Interactive Controller (Commits 4–5)
* **Goal:** Implement the line-based text protocol handshake and interactive controller.
* **Key Decisions:**
  * Implemented an initial greeting `OK CONNECTED SID:3122\n` sent immediately upon `accept()`.
  * Enforced a stateful flag (`authenticated = 0`) that rejects any incoming command until valid `AUTH OPS-2213` credentials are confirmed.
* **Challenges & Solutions:**
  * Client input included trailing carriage return and newline characters (`\r\n`), causing `strcmp()` authentication checks to fail. Fixed by stripping delimiters using `strcspn(buffer, "\r\n")`.

---

### Stage 3: Remote Execution Whitelist & Periodic UDP Telemetry (Commits 6–7)
* **Goal:** Implement restricted system diagnostics and real-time monitoring channels.
* **Key Decisions:**
  * Avoided arbitrary shell execution. A strict whitelist (`DATE`, `UPTIME`, `DISKFREE`, `HOSTNAME`, `WHOAMI`) was hardcoded using an array of allowed commands. Unlisted inputs instantly trigger `ERR 002 COMMAND NOT ALLOWED SID:3122`.
  * Decoupled telemetric monitoring from the main TCP control stream by launching a dedicated UDP thread (`AF_INET`, `SOCK_DGRAM`) streaming metrics every 2 seconds.
* **Challenges & Solutions:**
  * Safely stopping the UDP background thread when receiving `MONITOR STOP` or client disconnects. Resolved by using a shared volatile flag `running` to break the broadcast loop cleanly.

---

### Stage 4: File Transfer Protocol, Thread-Safe Logging, and Final Integration (Commits 8–9)
* **Goal:** Implement robust file streaming, centralized logging, and compiler cleanup.
* **Key Decisions:**
  * Isolated all file uploads strictly under `./agentfiles/IT24102213/` to prevent directory traversal and overwrite risks.
  * Used binary-safe chunked transfers (`fread`/`fwrite`) driven by exact byte counts to avoid text delimiter corruption.
  * Wrapped file logging routines inside a `pthread_mutex_t` to guarantee serialized, race-free writing to `remoteops_IT24102213.log`.
* **Challenges & Solutions:**
  * GCC compiler generated `-Wformat-truncation` warnings when formatting command responses. Fixed by expanding transmission buffers to 1024 bytes and verifying string boundaries.
