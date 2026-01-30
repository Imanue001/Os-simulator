# OS Simulator (Multithreaded)

A comprehensive C++ simulation of core Operating System concepts. This project implements a multithreaded environment to manage process synchronization, resource allocation, and CPU scheduling.

## 🛠 Features Included

### 1. Process Synchronization (Producer-Consumer)
Uses a **Bounded Buffer** with POSIX semaphores (`sem_t`) and `std::mutex` to safely handle communication between the Producer thread (creating processes) and the CPU thread (executing them).

### 2. CPU Scheduling (Round Robin)
Implements a **Round Robin** scheduler with a configurable time quantum. It tracks process execution and generates a **Gantt Chart** to visualize the CPU's timeline.

### 3. Resource Management & Deadlock Avoidance
A dedicated `ResourceManager` tracks system resources. It ensures that processes only enter the ready queue if their resource demands can be met, preventing system-wide deadlocks.

### 4. Concurrency Control
* **Thread Safety**: Uses `std::lock_guard` and `std::mutex` to prevent data races.
* **Atomic Operations**: Uses `std::atomic` for global control signals and `__sync_fetch_and_add` for thread-safe PID generation.

---

## 💻 Technical Stack
* **Language:** C++11 or higher
* **Libraries:** `<thread>`, `<mutex>`, `<semaphore.h>`, `<vector>`, `<deque>`
* **Concepts:** Multithreading, Synchronization Primitives, Resource Allocation, Scheduling Algorithms.

---

## 🚀 How to Run

### Compilation
Since this uses threads and semaphores, you must link the `pthread` library:
```bash
g++ main.cpp -o os_sim -lpthread
