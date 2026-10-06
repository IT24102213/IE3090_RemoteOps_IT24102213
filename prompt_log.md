# AI Interaction Prompt Log (Part 1 - RemoteOps)

**Student ID:** IT24102213  
**Module:** IE3090 – Network Programming  
**Level of AI Collaboration:** CLEAR Level 3  

---

### Prompt Session 1: Socket Setup & Concurrency
* **Tool Used:** Gemini / ChatGPT  
* **Prompt:** *"Show me how to create a multi-threaded TCP server in C using sys/socket.h and pthread_create where each client is handled in a separate thread."*  
* **AI Output:** Provided basic `socket()`, `bind()`, `listen()`, `accept()` loop and a `pthread_create()` call passing a pointer to the socket descriptor.  
* **Evaluation & Human Modification:** The AI code passed `&client_sock` directly into `pthread_create()`, creating a serious race condition if new clients connected rapidly. I modified the code to dynamically allocate memory via `malloc(sizeof(struct client_info))` for each inbound connection.

---

### Prompt Session 2: Line Parsing and Delimiters
* **Tool Used:** Gemini / ChatGPT  
* **Prompt:** *"How to parse line-based text commands ending with \n or \r\n in C socket recv buffer without cutting tokens?"*  
* **AI Output:** Suggested `strtok()` with `\r\n`.  
* **Evaluation & Human Modification:** `strtok()` is not thread-safe. I rejected `strtok()` and used `strcspn()` to strip line breaks, alongside bounded token parsing functions to maintain thread safety.

---

### Prompt Session 3: Remote Command Whitelist Security
* **Tool Used:** Gemini / ChatGPT  
* **Prompt:** *"How to safely execute bash commands from C using popen and return output to a socket client?"*  
* **AI Output:** Generated a generic function executing arbitrary `popen(cmd, "r")`.  
* **Evaluation & Human Modification:** This violated the core security requirements of the assignment brief. I discarded the unrestricted execution logic and constructed an explicit whitelist lookup table (`DATE`, `UPTIME`, `DISKFREE`, `HOSTNAME`, `WHOAMI`). Any command not matching the whitelist is rejected with `ERR 002 COMMAND NOT ALLOWED SID:3122`.

---

### Prompt Session 4: Makefile Structure and Warnings
* **Tool Used:** Gemini / ChatGPT  
* **Prompt:** *"Create a personalized Makefile for agent_213.c and controller_213.c with -Wall -Wextra -pthread flags."*  
* **AI Output:** Standard Makefile with targets `agent_213`, `controller_213`, and `clean`.  
* **Evaluation & Human Modification:** Used directly as base for `Makefile_213`. During build, resolved a `-Wformat-truncation` warning in `snprintf()` by expanding string buffers to 1024 bytes.
