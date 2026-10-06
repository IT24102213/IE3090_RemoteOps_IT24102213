Structured Reflection on AI Collaboration and Learning

Registration Number: IT24102213
Module: IE3090 – Network Programming
AI Collaboration Level: CLEAR Level 3

1. AI Tools Used and Project Stages
I used generative AI tools (ChatGPT and Google Gemini) while implementing the RemoteOps framework. Specifically during the early conception stages, initial boilerplate code for POSIX stream sockets, parameter configuration for setsockopt(SO_REUSEADDR) documentation, and initial targets for Makefile_213. Later on during testing, I consulted AI to fix compiler warnings about format truncation.



2. Strengths and Limitations of AI
The AI models were excellent at providing syntactically correct templates for low-level system call wrappers and explaining POSIX threading syntax (pthread_create, pthread_detach). However, the AI models had significant shortcomings regarding assignment-specific protocol constraints:

2.1 Missing session tags: Repeatedly, the AI omitted the mandatory personalized Session ID tag (SID:3122) from command response headers, defaulting to generic strings.

2.2 Security whitelist violations: When asked to implement the EXEC handler, AI generated an open-ended, dangerous shell dispatcher using popen(cmd, "r"). This violated the explicit 5-command security whitelist and introduced severe Remote Code Execution (RCE) vulnerabilities.
2.3 Concurrency pitfalls: Early AI snippets passed local socket file descriptors by reference into newly spawned threads, creating race conditions during rapid concurrent client connections.




3. Human Adaptation, Modifications, and Rejections
To adhere to the assignment brief, I refactored and superseded the AI-generated code:

3.1 I discarded the generic command execution logic and implemented a deterministic whitelist validation array restricting commands exclusively to DATE, UPTIME, DISKFREE, HOSTNAME, and WHOAMI

3.2 I modified the connection dispatching loop in agent_213.c to dynamically allocate memory on the heap (malloc(sizeof(struct client_info))) for each inbound client, eliminating connection descriptor collisions


3.3 I manually updated every server transmission buffer to format and append the personalized SID:3122 tag.



4. Key Learning and Understanding Gained
Completing this assignment clarified low-level network programming and operating system interactions:

4.1 Stream Framing: I recognized that TCP provides a continuous stream rather than distinct message boundaries, requiring explicit buffer sanitization and delimiter stripping (strcspn).

4.2 Thread Synchronization: I ensured file logging operations to remoteops_IT24102213.log are serialized via pthread_mutex_t to prevent log corruption.

4.3 Security Discipline: I learned that network software must never implicitly trust client input or raw AI outputs without rigorous verification, whitelisting, and state management.

