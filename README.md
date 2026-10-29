# 🚀 FastFileFinder

A high-performance, multi-threaded command-line file search engine built with **Modern C++17**.

## 💡 Why This Project?
Traditional sequential file scanning is slow on modern NVMe drives. **FastFileFinder** leverages parallel processing by utilizing a custom-built **Thread Pool**, distributing directory traversal across all available CPU cores.

## 🛠 Architecture
- **Thread Pool Pattern**: Avoids the overhead of creating/destroying threads by reusing a worker pool.
- **Atomic Operations**: High-speed, lock-free file counting.
- **Recursive Task Spawning**: Each subdirectory found is pushed as a new task to the pool, maximizing concurrency.
- **RAII & Modern STL**: Zero raw pointers, safe resource management using `std::filesystem` and `std::mutex`.

## 🚀 Features
- [x] **Blazing Fast**: Multi-threaded directory crawling.
- [x] **Smart Search**: Case-insensitive partial name matching.
- [x] **Resource Efficient**: Uses `hardware_concurrency` to adapt to your CPU.
- [x] **Cross-Platform**: Built using pure C++ STL.

## 🔨 Build Instructions
```bash
mkdir build && cd build
cmake ..
make