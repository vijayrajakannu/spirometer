# Performance Metrics for RTOS-Based M4 Single-Core System

This document summarizes the runtime performance, scheduling behavior, memory usage, and application-level output metrics measured on a **Cortex-M4 single-core system running FreeRTOS**. All values are obtained from on-device instrumentation and debugger inspection.

---

## 1. Real-Time Performance Metrics

### 1.1 End-to-End Latency

**Definition:** Time from sensor data acquisition to final computed output availability.

| Metric | M4 Single-Core RTOS |
|------|---------------------|
| Average latency (ms) | **0.0097** |
| Maximum latency (ms) | **1.0** |

---

### 1.2 Worst Case Execution Time (WCET)

**Definition:** Maximum observed execution time of the critical task chain under worst-case conditions.

| System | WCET (ms) | WCET (Clock Cycles) |
|------|-----------|---------------------|
| M4 Single-Core RTOS | **< 1 ms (tick-limited)** | **< 84,000 cycles** |

> **Note:** WCET values are limited by the 1 ms RTOS tick resolution. Tasks complete within a single tick.

---

### 1.3 Deadline Miss Rate

**Definition:** Percentage of task instances that miss their deadlines.

| System | Total Samples | Missed Deadlines | Miss Rate (%) |
|------|---------------|------------------|---------------|
| M4 Single-Core RTOS | **1547** | **0** | **0.0%** |

---

## 2. Scheduling and Timing Metrics

### 2.1 Jitter

**Definition:** Variation in task start time relative to its expected periodic schedule.

| Metric | M4 Single-Core RTOS (ms) |
|------|--------------------------|
| Average jitter | **10.0** |
| Maximum jitter | **10.0** |

> Measured at RTOS tick granularity (1 ms).

---

### 2.2 Task Response Time

**Definition:** Time from task release (interrupt/event) to task completion.

| Task | M4 Single-Core RTOS (ms) | Clock Cycles (M4) |
|------|--------------------------|-------------------|
| Signal Processing Task | **0** | **< 84,000** |
| Integration Task | **0** | **< 84,000** |
| Parameter Computation Task | **0** | **< 84,000** |

> Zero-millisecond values indicate completion within the same RTOS tick.

---

### 2.3 Context Switch Rate

**Definition:** Number of context switches per second, indicating scheduling overhead.

| System | Context Switches / Second | Avg Cycles / Switch |
|------|----------------------------|---------------------|
| M4 Single-Core RTOS | **Not instrumented** | — |

---

## 3. CPU Utilization and Efficiency

### 3.1 CPU Utilization

**Definition:** Percentage of CPU time spent executing non-idle tasks.

| CPU / Core | M4 Single-Core RTOS (%) |
|-----------|--------------------------|
| Core 0 | **Invalid (counter mismatch)** |

---

### 3.2 Idle Time Percentage

**Definition:** Percentage of time CPU remains idle (low-power or idle task).

| System | Idle Time (%) | Idle Cycles (%) |
|------|----------------|-----------------|
| M4 Single-Core RTOS | **Invalid (division by zero)** | — |

> Idle hook is active; total tick counter was not synchronized during measurement.

---

## 4. Memory Usage and Safety Metrics

### 4.1 Total SRAM Footprint

**Definition:** Breakdown of SRAM usage across RTOS components.

| Memory Component | M4 Single-Core RTOS (KB) |
|-----------------|--------------------------|
| RTOS kernel | — |
| Task stacks | **≈ 2.6** |
| Heap (RTOS objects) | **5.19** |
| IPC buffers | — |
| **Total SRAM** | **≈ 7.8 KB** |

---

### 4.2 Heap Usage

**Definition:** Dynamic memory consumption behavior during runtime.

| Metric | M4 Single-Core RTOS (KB) |
|-------|--------------------------|
| Heap used | **5.19** |
| Minimum free heap | **4.81** |

*(5312 B used, 4928 B free)*

---

### 4.3 Stack High Water Mark

**Definition:** Maximum stack usage observed per task (safety margin indicator).

| Task | M4 Single-Core RTOS (bytes) |
|------|------------------------------|
| Signal Processing Task | **728** |
| Integration Task | **1480** |
| Parameter Computation Task | **428** |

---

## 5. Application-Level Output Consistency Metrics

### 5.1 Spirometry Parameter Consistency

**Definition:** Consistency of computed physiological parameters on the M4 RTOS platform.

| Parameter | M4 Single-Core RTOS |
|-----------|---------------------|
| FEV₁ (L) | Measured |
| FVC (L) | Measured |
| PEF (L/s) | Measured |

---

### Notes

- Timing values are constrained by the 1 ms FreeRTOS tick resolution.
- Sub-millisecond measurements require DWT cycle counter instrumentation.
- All results were obtained from live runtime data and debugger inspection.
