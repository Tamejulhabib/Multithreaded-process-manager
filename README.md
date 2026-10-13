# Multithreaded Process Manager Simulator

A multithreaded process management simulator developed in **C using POSIX Threads (pthreads)**. This project demonstrates fundamental operating system concepts, including process creation, parent-child relationships, process state transitions, synchronization, zombie processes, and process monitoring.

## Project Overview

The Multithreaded Process Manager Simulator models how an operating system manages multiple processes concurrently.

The system uses worker threads to execute process management commands from input files. A shared Process Control Block (PCB) table stores process information, while mutexes and condition variables coordinate concurrent access and monitoring.

The project provides a practical demonstration of operating system process lifecycle management and multithreaded programming.

## Key Features

- **Process Creation:** Simulates process creation using `pm_fork()`.
- **Process Termination:** Handles process exits using `pm_exit()`.
- **Process Waiting:** Allows parent processes to wait for terminated children using `pm_wait()`.
- **Process Killing:** Supports process termination through `pm_kill()`.
- **Process Monitoring:** Displays process information and state transitions.
- **Multithreading:** Uses POSIX threads to execute commands concurrently.
- **Thread Synchronization:** Uses mutexes and condition variables to protect shared data.
- **Zombie Process Management:** Simulates zombie processes and their cleanup.
- **Process Snapshots:** Records changes to the process table.

## Technologies Used

| Technology | Purpose |
|---|---|
| C | Core programming language |
| POSIX Threads | Multithreaded execution |
| Mutex Locks | Shared resource synchronization |
| Condition Variables | Thread coordination |
| GCC | Compilation |
| Linux / Ubuntu | Execution environment |

## System Architecture

The simulator consists of three main components:

**1. Worker Threads**

Each worker thread reads commands from an input file and executes simulated process management operations.

**2. Shared Process Table**

Maintains Process Control Blocks containing process identifiers, parent identifiers, process states, exit statuses, and child relationships.

**3. Monitor Thread**

Observes process changes and generates snapshots for monitoring and debugging.

### Workflow

```text
       Input Command Files
               |
               v
        Worker Threads
               |
               v
     Process Management API
               |
               v
      Shared PCB Table
       (Mutex Protected)
               |
               v
        Monitor Thread
               |
               v
       Process Snapshots
```

## Supported Process States

| State | Description |
|---|---|
| RUNNING | Process is currently active |
| BLOCKED | Process is waiting for an event |
| ZOMBIE | Process has terminated but has not been reaped |
| TERMINATED | Process has completed its lifecycle |

## Project Structure

```text
multithreaded-process-manager/
|
|-- pm_sim.c
|-- thread0.txt
|-- thread1.txt
|-- thread2.txt
|-- snapshots.txt
|-- README.md
```

## Installation and Execution

### Prerequisites

- Linux or Ubuntu (WSL can be used on Windows)
- GCC compiler
- POSIX Threads support

### Step 1: Clone the Repository

```bash
git clone https://github.com/YOUR_USERNAME/multithreaded-process-manager.git
cd multithreaded-process-manager
```

Replace `YOUR_USERNAME` with your GitHub username.

### Step 2: Compile

```bash
gcc -Wall -Wextra -pthread pm_sim.c -o pm_sim
```

### Step 3: Run

```bash
./pm_sim thread0.txt thread1.txt thread2.txt
```

The simulator executes commands from the input files and generates process snapshots.

## Example Input

**thread1.txt**

```text
fork 1
sleep 200
fork 1
wait 1 -1
```

**thread2.txt**

```text
sleep 100
fork 1
sleep 300
exit 2 10
```

## Process Management Operations

| Command | Description |
|---|---|
| `fork` | Creates a simulated child process |
| `exit` | Terminates a simulated process |
| `wait` | Waits for a child process to exit |
| `kill` | Terminates a specified process |
| `ps` | Displays process information |
| `sleep` | Introduces a delay during execution |

## Learning Outcomes

Through this project, we explored:

- Operating system process management
- Multithreaded application development
- Process Control Block design
- Parent-child process relationships
- Thread synchronization and race conditions
- Mutex locks and condition variables
- Zombie processes and resource cleanup
- Concurrent execution and process monitoring

## Limitations

- This project simulates process operations rather than creating actual operating system processes.
- The process table supports a maximum of 64 process slots.
- Thread scheduling may cause different execution orders between runs.
- The simulator is intended for educational purposes rather than production process management.

## Future Improvements

- Implement more advanced process scheduling algorithms.
- Add priority-based process management.
- Improve concurrent monitoring and event handling.
- Introduce deadlock detection.
- Add automated unit and stress tests.
- Develop a graphical interface for process visualization.

## Contributors

Developed as part of the **CSE321 Operating Systems** course project.

- Zead Raihan
- Fahim Ahmad Rafi
- Sayeeb Hossain

## License

This project is intended for educational and academic purposes. No open-source license has been assigned.
