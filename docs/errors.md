# Error and Warning Code Catalog

This catalog documents the structured error codes (`E-xxxx`) and warning codes (`W-xxxx`) reserved for Hikari-Machine subsystems.

> [!NOTE]
> **Status: Reserved / Specification Catalog**  
> In Phase 0, the runtime engine systems are not yet implemented. The codes below are reserved specifications defined for implementation beginning in Phase 1 (Core) and subsequent phases.

## Format & Severity

- **W-xxxx (Warnings)**: Non-fatal anomalous conditions, unexpected performance stalls, or cache invalidation events. These require developer investigation if observed during regular operation.
- **E-xxxx (Errors)**: Recoverable failures returned as `Result<T, Error>` values, or fatal invariant violations trapped via assertions.

---

## 0100–0199: Subsystem 01 — Framework & Conductor

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `W-0101` | Warning | Implemented | Conductor cache invalidation during commit. | Stale entities or moved targets since `prepare()` ran. | Standby `prepare()` budget may be under-allocated or world churn is excessive. |
| `W-0102` | Warning | Implemented | Mode preparation budget overrun. | Standby `Mode::prepare()` exceeded allotted microsecond budget. | Reduce per-frame batch size in standby mode. |
| `E-0101` | Error | Implemented | Invalid mode state transition attempted. | State transition requested while another transition is in flight or unready. | Guard state transitions with transition completion checks. |
| `E-0102` | Error | Implemented | Unregistered system group accessed. | Mode requested activation of a non-existent system group tag. | Verify system group registration during startup. |
| `E-0103` | Error | Implemented | Mode commitment or pre-commit cache validation failed. | Entity referenced in standby cache was deleted during preparation. | Handle stale entity references safely or recompute cache. |
| `E-0104` | Error | Implemented | Requested entity was not found or is dead. | Accessing entity handle after destruction. | Check entity lifecycle with `world.isAlive()`. |

---

## 0200–0299: Subsystem 02 — Core & Memory

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `E-0200` | Error | Implemented | Invalid function argument or precondition violation. | Null pointer passed or parameter range violation. | Validate input parameters before calling function. |
| `E-0201` | Error | Implemented | Generational handle resolution failed. | Accessing an index whose generation has rolled over or expired. | Check entity or asset lifecycle before dereferencing handle. |
| `E-0202` | Error | Implemented | Memory capacity exhausted in target allocator. | Subsystem arena or pool exhausted without expansion allowance. | Pre-allocate larger capacity or audit memory leaks. |
| `E-0203` | Error | Implemented | Container index or offset out of bounds. | Access index exceeds size or capacity of target container. | Ensure index is within [0, size). |
| `E-0204` | Error | Implemented | Operation attempted on invalid or uninitialized state. | Calling method before initialization or after disposal. | Verify lifecycle state transitions. |
| `E-0205` | Error | Implemented | Requested feature or backend operation not supported. | Feature unsupported on current platform or driver tier. | Query capability flags before dispatching operation. |
| `E-0210` | Error | Implemented | Platform subsystem initialization failed. | SDL3 or OS display subsystem failed to initialize. | Verify system display server and driver configurations. |
| `E-0211` | Error | Implemented | Window creation or display operation failed. | Invalid window flags, unsupported resolution, or headless failure. | Verify window parameters or enable headless/dummy driver. |
| `W-0201` | Warning | Reserved | Frame allocator high watermark exceeded 80%. | Excess temporary allocations in current frame. | Increase frame allocator capacity or move allocations to arenas. |

---

## 0300–0399: Subsystem 03 — RHI & Rendering

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `W-0301` | Warning | Reserved | Pipeline cache miss at runtime. | Shader permutation not pre-warmed during standby preparation. | Pre-warm shader pipeline in standby mode `prepare()`. |
| `E-0300` | Error | Implemented | RHI subsystem or backend initialization failed. | Backend driver missing, unsupported graphics API, or init error. | Check device compatibility and fallback to supported RHI backend. |
| `E-0301` | Error | Implemented | Graphics device creation failure. | Physical device lacks required Vulkan 1.3 features or extensions. | Verify driver support and minimum hardware tier. |
| `E-0302` | Error | Implemented | Out of GPU device local memory. | VMA allocation failed to find free memory block. | Audit texture/buffer footprints and release unused GPU resources. |
| `E-0303` | Error | Implemented | RHI resource creation failed. | Invalid descriptor parameters or hardware limit exceeded. | Check GPU capabilities (`GpuCaps`) before allocating resources. |
| `E-0305` | Error | Implemented | Command buffer recording or queue submission error. | Submitting invalid command list or device lost during submission. | Validate command list state prior to submission. |

---

## 0400–0499: Subsystem 04 — Data & Assets

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `W-0401` | Warning | Reserved | Asset hot reload failed validation. | Syntax error or schema mismatch in edited TOML/JSON. | Correct syntax error in asset data file. |
| `E-0401` | Error | Reserved | Missing required asset file. | File path not found in mounted virtual filesystem. | Verify asset cooking output and asset relative path. |
| `E-0402` | Error | Reserved | Schema validation failure. | Content file contains unknown fields or invalid value ranges. | Update data file to match schema specification. |
