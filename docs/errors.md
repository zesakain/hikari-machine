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
| `W-0101` | Warning | Reserved | Conductor cache invalidation during commit. | Stale entities or moved targets since `prepare()` ran. | Standby `prepare()` budget may be under-allocated or world churn is excessive. |
| `W-0102` | Warning | Reserved | Mode preparation budget overrun. | Standby `Mode::prepare()` exceeded allotted microsecond budget. | Reduce per-frame batch size in standby mode. |
| `E-0101` | Error | Reserved | Invalid mode state transition attempted. | State transition requested while another transition is in flight. | Guard state transitions with transition completion checks. |
| `E-0102` | Error | Reserved | Unregistered system group accessed. | Mode requested activation of a non-existent system group tag. | Verify system group registration during startup. |

---

## 0200–0299: Subsystem 02 — Core & Memory

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `W-0201` | Warning | Reserved | Frame allocator high watermark exceeded 80%. | Excess temporary allocations in current frame. | Increase frame allocator capacity or move allocations to arenas. |
| `E-0201` | Error | Reserved | Generational handle resolution failed. | Accessing an index whose generation has rolled over or expired. | Check entity or asset lifecycle before dereferencing handle. |
| `E-0202` | Error | Reserved | Out of arena memory capacity. | Subsystem arena exhausted without dynamic expansion allowance. | Pre-allocate larger arena at scene / mode init. |

---

## 0300–0399: Subsystem 03 — RHI & Rendering

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `W-0301` | Warning | Reserved | Pipeline cache miss at runtime. | Shader permutation not pre-warmed during standby preparation. | Pre-warm shader pipeline in standby mode `prepare()`. |
| `E-0301` | Error | Reserved | Graphics device creation failure. | Physical device lacks required Vulkan 1.3 features or extensions. | Verify driver support and minimum hardware tier. |
| `E-0302` | Error | Reserved | Out of GPU device local memory. | VMA allocation failed to find free memory block. | Audit texture/buffer footprints and release unused GPU resources. |

---

## 0400–0499: Subsystem 04 — Data & Assets

| Code | Severity | Status | Description | Typical Cause | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `W-0401` | Warning | Reserved | Asset hot reload failed validation. | Syntax error or schema mismatch in edited TOML/JSON. | Correct syntax error in asset data file. |
| `E-0401` | Error | Reserved | Missing required asset file. | File path not found in mounted virtual filesystem. | Verify asset cooking output and asset relative path. |
| `E-0402` | Error | Reserved | Schema validation failure. | Content file contains unknown fields or invalid value ranges. | Update data file to match schema specification. |
