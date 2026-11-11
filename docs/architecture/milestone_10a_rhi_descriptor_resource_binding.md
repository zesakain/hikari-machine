# Milestone 10A: RHI Descriptor & Resource Binding Architecture

## 1. Overview

Milestone 10A establishes the foundation for hardware-neutral, zero-allocation resource binding across Hikari Engine. It bridges shader pipelines with uniform buffers, storage buffers, sampled images, and samplers while strictly adhering to Rule 3 (no graphics API headers outside `/engine/rhi`) and Rule 5 (zero heap allocations on the hot path).

---

## 2. Resource Binding Model

### 2.1 Generational Resource Handles
All descriptor entities are tracked as 64-bit `GenerationalHandle`s to ensure safe reuse and prevent use-after-free bugs:
- `DescriptorSetLayoutHandle`: Declares immutable binding topologies for shaders.
- `DescriptorPoolHandle`: Manages GPU descriptor heaps and allocation budgets.
- `DescriptorSetHandle`: Represents an active binding instance containing GPU descriptor descriptors.

### 2.2 Lifecycle & Ownership Hierarchy
```
           +-----------------------------+
           |     DescriptorPoolHandle    |
           +--------------+--------------+
                          | allocates
                          v
           +-----------------------------+
           |     DescriptorSetHandle     |
           +--------------+--------------+
                          | bound to
                          v
+--------------------+   bind   +------------------------+
| PipelineLayoutDesc +--------> | Vulkan/NullCommandList |
+--------------------+          +------------------------+
```
- **Pool Ownership:** Descriptor sets are strictly owned by their allocating pool.
- **Cascade Invalidation:** Resetting (`resetDescriptorPool`) or destroying (`destroyDescriptorPool`) a pool immediately invalidates all generational handles of descriptor sets allocated from it.
- **Layout Independency:** Descriptor set layouts can be shared across multiple pipelines and pools without coupling.

---

## 3. Zero-Allocation Hot-Path Execution

In compliance with `AGENTS.md` Section 6:
- `bindDescriptorSets` on both `NullCommandList` and `VulkanCommandList` uses a fixed stack array (`kMaxSetsOnStack = 8`) to assemble native `VkDescriptorSet` handles during command recording.
- `updateDescriptorSet` on `VulkanDevice` utilizes stack-allocated arrays (`kMaxStackWrites = 8`, `kMaxStackInfos = 16`) to batch `VkWriteDescriptorSet`, `VkDescriptorBufferInfo`, and `VkDescriptorImageInfo` structs, avoiding heap allocations in standard per-frame updates.

---

## 4. Pipeline Layout Integration

`PipelineLayoutDesc` supports:
1. `pushConstants`: Push constant ranges for low-latency uniform updates (up to 128 bytes).
2. `descriptorSetLayoutHandles`: Direct handles to pre-created `DescriptorSetLayoutHandle`s.
3. `descriptorSetLayouts`: Inline descriptors for compatibility.

During `createPipelineLayout`, all descriptor layout handles are validated and resolved into the Vulkan pipeline layout, ensuring clean integration with existing push constants.

---

## 5. Verification & Testing

The resource binding system is verified by two test suites:
1. `tests/unit/rhi/null_rhi_test.cpp` (`testDescriptorLifecycleAndBinding`): Headless unit testing for layout validation, pool exhaustion (`ErrorCode::OutOfMemory`), buffer/image write validation, and command binding.
2. `tests/unit/rhi/vulkan_rhi_test.cpp` (`testVulkanDescriptorLifecycleAndBinding`): Live GPU execution with Vulkan Validation Layers enabled on NVIDIA P106-100, verifying hardware resource binding and command recording.
