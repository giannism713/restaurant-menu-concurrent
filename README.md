````markdown
# HY486 Project 1: Concurrent Restaurant Order Processing System

## Overview
This project implements a concurrent restaurant order processing system using C++20 and POSIX threads. The system simulates the workflow of a restaurant, where multiple threads (Districts, Agents, and Cooks) interact through shared data structures to process customer orders efficiently and safely in a multi-threaded environment.

## Key Features
- **Highly concurrent:** Utilizes lock-free and fine-grained locking data structures for high performance.
- **Custom data structures:** Implements a lock-free elimination stack, a thread-safe queue, and a lazy-synchronized linked list.
- **Configurable:** The number of threads can be set in the `Makefile` via the `N_THREADS` variable.

## Data Structures Used

### 1. Pending Orders Stack (Lock-Free Elimination Stack)
- **Type:** Unbounded, lock-free stack with elimination backoff.
- **Purpose:** Stores pending customer orders. District threads push new orders, and Agent threads pop them for further processing.
- **Concurrency:** Uses atomic operations and an elimination array to reduce contention and improve scalability.

### 2. Under-Preparation Orders Queue (Thread-Safe Queue)
- **Type:** Unbounded, thread-safe queue with fine-grained locking (separate head/tail locks).
- **Purpose:** Holds orders that are being prepared. Agent threads enqueue orders, and Cook threads dequeue them for completion.
- **Concurrency:** Uses two mutexes to allow concurrent enqueue and dequeue operations.

### 3. Completed Orders List (Lazy-Synchronized Linked List)
- **Type:** Sorted, singly-linked list with lazy synchronization.
- **Purpose:** Stores completed orders for each district. Cook threads insert completed orders, and District threads search for their orders to verify completion.
- **Concurrency:** Uses per-node mutexes and logical deletion (marked flag) for safe concurrent updates.

## Thread Roles
- **District Threads:** Generate and push new orders, then wait for their completion.
- **Agent Threads:** Pop orders from the stack and enqueue them for preparation.
- **Cook Threads:** Dequeue orders, mark them as completed, and insert them into the appropriate district's completed orders list.

## How to Build and Run
1. **Configure the number of threads** (optional):
	- Edit the `Makefile` and set the `N_THREADS` variable (must be a multiple of 4).
2. **Build the project:**
	```bash
	make
	```
3. **Run the program:**
	```bash
	./main
	```
4. **Clean build artifacts:**
	```bash
	make clean
	```

## Output
The program prints PASS/FAIL checks for:
- Pending orders stack emptiness
- Under-preparation queue emptiness
- Completed orders list size, sum, and validity for each district

## File Structure
- `main.cpp` – Main implementation and entry point
- `Makefile` – Build configuration
- `README.md` – Project documentation

## Requirements
- C++20 compiler (e.g., g++)
- POSIX threads (pthreads)
- Linux environment

## Notes
- The project demonstrates advanced concurrent programming techniques and custom synchronization mechanisms.
- The code is well-commented for educational purposes.
