# Contributing to Quantum-Execution-Engine

## Language Layers

### Rust Order Router
```bash
cd rust_router  # or project root
cargo build --release
cargo test
```

### C++ Risk Kernel
```bash
g++ -std=c++20 -O3 -march=native risk_kernel.cpp -o risk_kernel
```

### Java FIX Adapter
```bash
mvn package  # or gradlew build
```

### TypeScript Dashboard
```bash
npm install && npm run build
```

## Architecture Invariants
- The Rust router must never block; use async/await or `spawn_blocking` for I/O
- C++ risk kernel is single-threaded — all concurrency is at the router layer
- Java FIX adapter handles session management; business logic belongs in Rust
- Dashboard is read-only; it never sends trade commands directly

## Performance SLAs
| Component | Target Latency |
|---|---|
| Rust order router (p99) | < 50 µs |
| C++ risk kernel (p99) | < 10 µs |
| Java FIX round trip | < 500 µs |
