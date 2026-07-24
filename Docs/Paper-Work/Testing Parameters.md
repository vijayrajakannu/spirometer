# Performance Metrics for RTOS-Based Single-Core and Dual-Core Systems

## 1. Real-Time Performance Metrics

### 1.1 End-to-End Latency

**Definition:** Time from sensor data acquisition to final computed output availability.

|Metric|M4 Single-Core RTOS|M7 Single-Core RTOS|M4/M7 Dual-Core RTOS|
|---|---|---|---|
|Average latency (ms)||||
|Maximum latency (ms)||||

### 1.2 Worst Case Execution Time (WCET)

**Definition:** Maximum observed execution time of the critical task chain under worst-case conditions.

|System|WCET (ms)|WCET (Clock Cycles)|
|---|---|---|
|M4 Single-Core RTOS|||
|M7 Single-Core RTOS|||
|M4/M7 Dual-Core RTOS|||

### 1.3 Deadline Miss Rate

**Definition:** Percentage of task instances that miss their deadlines.

|System|Total Samples|Missed Deadlines|Miss Rate (%)|
|---|---|---|---|
|M4 Single-Core RTOS||||
|M7 Single-Core RTOS||||
|M4/M7 Dual-Core RTOS||||

## 2. Scheduling and Timing Metrics

### 2.1 Jitter

**Definition:** Variation in task start time relative to its expected periodic schedule.

|Metric|M4 Single-Core RTOS (ms)|M7 Single-Core RTOS (ms)|M4/M7 Dual-Core RTOS (ms)|
|---|---|---|---|
|Average jitter||||
|Maximum jitter||||

### 2.2 Task Response Time

**Definition:** Time from task release (interrupt/event) to task completion.

|Task|M4 Single-Core RTOS (ms)|M7 Single-Core RTOS (ms)|M4/M7 Dual-Core RTOS (ms)|Clock Cycles (M4)|Clock Cycles (M7)|
|---|---|---|---|---|---|
|Signal Processing Task||||||
|Integration Task||||||
|Parameter Computation Task||||||

### 2.3 Context Switch Rate

**Definition:** Number of context switches per second, indicating scheduling overhead.

|System|Context Switches / Second|Avg Cycles / Switch|
|---|---|---|
|M4 Single-Core RTOS|||
|M7 Single-Core RTOS|||
|M4/M7 Dual-Core RTOS|||

## 3. CPU Utilization and Efficiency

### 3.1 CPU Utilization

**Definition:** Percentage of CPU time spent executing non-idle tasks.

|CPU / Core|M4 Single-Core RTOS (%)|M7 Single-Core RTOS (%)|M4/M7 Dual-Core RTOS (%)|
|---|---|---|---|
|Core 0||||
|Core 1|—|—||

### 3.2 Idle Time Percentage

**Definition:** Percentage of time CPU remains idle (low-power or idle task).

|System|Idle Time (%)|Idle Cycles (%)|
|---|---|---|
|M4 Single-Core RTOS|||
|M7 Single-Core RTOS|||
|M4/M7 Dual-Core RTOS|||

## 4. Memory Usage and Safety Metrics

### 4.1 Total SRAM Footprint

**Definition:** Breakdown of SRAM usage across RTOS components.

|Memory Component|M4 Single-Core RTOS (KB)|M7 Single-Core RTOS (KB)|M4/M7 Dual-Core RTOS (KB)|
|---|---|---|---|
|RTOS kernel||||
|Task stacks||||
|Heap (RTOS objects)||||
|IPC buffers||||
|**Total SRAM**||||

### 4.2 Heap Usage

**Definition:** Dynamic memory consumption behavior during runtime.

|Metric|M4 Single-Core RTOS (KB)|M7 Single-Core RTOS (KB)|M4/M7 Dual-Core RTOS (KB)|
|---|---|---|---|
|Heap used||||
|Minimum free heap||||

### 4.3 Stack High Water Mark

**Definition:** Maximum stack usage observed per task (safety margin indicator).

|Task|M4 Single-Core RTOS (bytes)|M7 Single-Core RTOS (bytes)|M4/M7 Dual-Core RTOS (bytes)|
|---|---|---|---|
|Signal Processing Task||||
|Integration Task||||
|Parameter Computation Task||||

## 5. Application-Level Output Consistency Metrics

### 5.1 Spirometry Parameter Consistency

**Definition:** Comparison of computed physiological parameters across architectures.

|Parameter|M4 Single-Core RTOS|M7 Single-Core RTOS|M4/M7 Dual-Core RTOS|Max Deviation (%)|Std Deviation|
|---|---|---|---|---|---|
|FEV₁ (L)||||||
|FVC (L)||||||
|PEF (L/s)||||||
