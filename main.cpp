#include <iostream>
#include <vector>
#include <deque>
#include <map>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <mutex>
#include <semaphore.h>
#include <algorithm>
#include <iomanip>

/* =========================
   PROCESS STRUCTURE
   ========================= */
struct Process {
    int pid;
    int arrivalTime;
    int burstTime;
    int remainingTime;
    std::vector<int> maxDemand;

    Process(int pid_, int at, int bt, const std::vector<int>& req)
        : pid(pid_), arrivalTime(at), burstTime(bt),
          remainingTime(bt), maxDemand(req) {}
};

/* =========================
   GLOBAL CONTROL
   ========================= */
static std::atomic<bool> gRunning{false};
static std::atomic<bool> gStopAll{false};
static int gPidCounter = 1;
std::mutex gIoMtx; 

/* =========================
   RANDOM HELPERS
   ========================= */
static int rndInt(int lo, int hi){
    static thread_local std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

/* =========================
   BOUNDED BUFFER
   ========================= */
class BoundedBuffer {
private:
    std::vector<Process*> buf;
    int cap, head = 0, tail = 0;
    sem_t empty, full;
    std::mutex mtx;

public:
    explicit BoundedBuffer(int c) : buf(c, nullptr), cap(c) {
        sem_init(&empty, 0, c);
        sem_init(&full, 0, 0);
    }
    ~BoundedBuffer() {
        sem_destroy(&empty);
        sem_destroy(&full);
    }

    void push(Process* p) {
        sem_wait(&empty);
        {
            std::lock_guard<std::mutex> lock(mtx);
            buf[tail] = p;
            tail = (tail + 1) % cap;
        }
        sem_post(&full);
    }

    Process* pop() {
        // Non-blocking check for stop signal
        int val;
        sem_getvalue(&full, &val);
        if (val <= 0 && gStopAll) return nullptr;

        // Using a simple wait logic to avoid sem_timedwait portability issues
        while (true) {
            if (sem_trywait(&full) == 0) break;
            if (gStopAll) return nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Process* p;
        {
            std::lock_guard<std::mutex> lock(mtx);
            p = buf[head];
            buf[head] = nullptr;
            head = (head + 1) % cap;
        }
        sem_post(&empty);
        return p;
    }
};

/* =========================
   RESOURCE MANAGER
   ========================= */
class ResourceManager {
private:
    std::vector<int> available;
    std::map<int, std::vector<int>> allocMap;
    std::mutex mtx;

public:
    ResourceManager(const std::vector<int>& avail) : available(avail) {}

    bool requestResources(Process* p) {
        std::lock_guard<std::mutex> lock(mtx);
        for (size_t i = 0; i < available.size(); i++) {
            if (p->maxDemand[i] > available[i]) return false;
        }
        for (size_t i = 0; i < available.size(); i++) {
            available[i] -= p->maxDemand[i];
        }
        allocMap[p->pid] = p->maxDemand;
        return true;
    }

    void releaseAll(Process* p) {
        std::lock_guard<std::mutex> lock(mtx);
        if (allocMap.count(p->pid)) {
            for (size_t i = 0; i < available.size(); i++)
                available[i] += allocMap[p->pid][i];
            allocMap.erase(p->pid);
        }
    }

    std::vector<int> getAvailable() {
        std::lock_guard<std::mutex> lock(mtx);
        return available;
    }
};

/* =========================
   SCHEDULER
   ========================= */
class Scheduler {
private:
    int quantum, time = 0;
    std::deque<Process*> ready;
    std::vector<std::pair<int, int>> gantt;
    std::mutex mtx;

public:
    explicit Scheduler(int q) : quantum(q) {}

    void addReady(Process* p) {
        std::lock_guard<std::mutex> lock(mtx);
        ready.push_back(p);
    }

    int readyCount() {
        std::lock_guard<std::mutex> lock(mtx);
        return (int)ready.size();
    }

    Process* dispatch() {
        std::lock_guard<std::mutex> lock(mtx);
        if (ready.empty()) return nullptr;

        Process* p = ready.front();
        ready.pop_front();

        int slice = std::min(quantum, p->remainingTime);
        p->remainingTime -= slice;
        gantt.push_back({ p->pid, slice });
        time += slice;

        if (p->remainingTime > 0) {
            ready.push_back(p);
            return nullptr;
        }
        return p;
    }

    void printGantt() {
        std::lock_guard<std::mutex> lock(mtx);
        if (gantt.empty()) { std::cout << "\nGantt chart is empty.\n"; return; }
        std::cout << "\n=== GANTT CHART ===\n|";
        for (auto& g : gantt) std::cout << " P" << g.first << " |";
        std::cout << "\n0";
        int t = 0;
        for (auto& g : gantt) { t += g.second; std::cout << std::setw(5) << t; }
        std::cout << "\n";
    }
};

/* =========================
   THREADS
   ========================= */
void producerThread(BoundedBuffer* buf) {
    while (!gStopAll) {
        if (gRunning) {
            int pid = __sync_fetch_and_add(&gPidCounter, 1);
            Process* p = new Process(pid, 0, rndInt(2, 6), { rndInt(1, 2), rndInt(1, 2), rndInt(1, 2) });
            buf->push(p);
            {
                std::lock_guard<std::mutex> lock(gIoMtx);
                std::cout << "[Producer] Created PID " << pid << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

void cpuThread(BoundedBuffer* buf, ResourceManager* rm, Scheduler* sch) {
    while (!gStopAll) {
        if (gRunning) {
            Process* p = buf->pop();
            if (p) {
                if (rm->requestResources(p)) {
                    sch->addReady(p);
                    {
                        std::lock_guard<std::mutex> lock(gIoMtx);
                        std::cout << "[CPU] Assigned resources to PID " << p->pid << std::endl;
                    }
                    if (Process* finished = sch->dispatch()) {
                        rm->releaseAll(finished);
                        {
                            std::lock_guard<std::mutex> lock(gIoMtx);
                            std::cout << "[CPU] Completed PID " << finished->pid << std::endl;
                        }
                        delete finished;
                    }
                }
                else {
                    buf->push(p); // Re-queue if resources aren't available
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

/* =========================
   MAIN
   ========================= */
int main() {
    BoundedBuffer buffer(10);
    ResourceManager rm({ 10, 10, 10 });
    Scheduler scheduler(2);

    std::thread prod(producerThread, &buffer);
    std::thread cpu(cpuThread, &buffer, &rm, &scheduler);

    int choice = 0;
    while (choice != 5) {
        {
            std::lock_guard<std::mutex> lock(gIoMtx);
            std::cout << "\n========= OS SIMULATOR =========";
            std::cout << "\nStatus: " << (gRunning ? "RUNNING" : "PAUSED");
            std::cout << "\n1) Run Simulation";
            std::cout << "\n2) Pause Simulation";
            std::cout << "\n3) View System State";
            std::cout << "\n4) View Gantt Chart";
            std::cout << "\n5) Exit";
            std::cout << "\nChoice: ";
        }
        if (!(std::cin >> choice)) break;

        switch (choice) {
        case 1: gRunning = true; break;
        case 2: gRunning = false; break;
        case 3: {
            auto a = rm.getAvailable();
            std::cout << "\n--- Resources Available: [" << a[0] << ", " << a[1] << ", " << a[2] << "]";
            std::cout << "\n--- Processes in Ready Queue: " << scheduler.readyCount() << std::endl;
            break;
        }
        case 4: scheduler.printGantt(); break;
        case 5: gStopAll = true; gRunning = true; break;
        }
    }

    if (prod.joinable()) prod.join();
    if (cpu.joinable()) cpu.join();

    std::cout << "Simulation terminated safely.\n";
    return 0;
}