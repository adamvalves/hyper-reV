# hyper-reV

**hyper-reV** is an advanced hypervisor-based memory introspection and kernel reverse engineering framework leveraging Microsoft Hyper-V. It provides guest-level virtual/physical memory reading/writing, virtual-to-physical address translation, Second-Level Address Translation (SLAT) stealth code hooks (EPT on Intel / NPT on AMD), physical memory page hiding, and a usermode kernel debugging interface.

By hooking into Hyper-V directly during the Windows boot sequence, **hyper-reV** operates beneath guest OS security mechanisms, maintaining full functionality even on systems running Hypervisor-Protected Code Integrity (HVCI).

---

## Technical Overview

### Core Capabilities
- **Direct Guest Memory Access**: Read and write guest physical and guest virtual memory.
- **Address Translation**: Translate guest virtual addresses (GVA) to guest physical addresses (GPA) for any target CR3.
- **SLAT Stealth Code Hooks (EPT & NPT)**: Transparent inline kernel code hooking using Page Table manipulation without modifying underlying physical page content visible to standard guest reads.
- **Memory Concealment**: Hide designated physical pages from guest kernel reads by remapping SLAT entries to dummy pages.
- **HVCI Compatibility**: Seamless operation under Windows virtualization-based security (VBS / HVCI).
- **Usermode Kernel Debugger CLI**: Interactive CLI client supporting kernel module export parsing, symbol aliasing, dynamic disassembly detour placement, and trap frame logging.

---

## Boot Architecture & Execution Flow

```
+------------------+     +------------------------+     +----------------------+
| UEFI Bootloader  | --> | bootmgfw.efi Intercept | --> | winload.efi Hooking  |
|  (uefi-boot.efi) |     |  (Restore original)    |     | (hvloader.dll hook)  |
+------------------+     +------------------------+     +----------------------+
                                                                   |
                                                                   v
+------------------+     +------------------------+     +----------------------+
| Usermode Debugger| <-- | Hyper-V VM Exit Detour | <-- | HvlLaunchHypervisor  |
|  (hypercall API) |     | (Attachment & SLAT)    |     |  (Hyper-V Launch)    |
+------------------+     +------------------------+     +----------------------+
```

### 1. UEFI Pre-Boot Hijack (`uefi-boot`)
- Replaces `bootmgfw.efi` in the EFI system partition with `uefi-boot.efi`.
- Copies and backs up the original `bootmgfw.efi` to restore metadata timestamps prior to OS load.
- Allocates an aligned UEFI memory heap to stage the hypervisor attachment payload (`hyperv-attachment`) and identity page tables (PML4/PDPT).
- Hooks `bootmgfw.efi!ImgpLoadPEImage` to intercept `winload.efi`.

### 2. Hyper-V Launch Detour (`hvloader.dll`)
- Intercepts `winload.efi!ImgpLoadPEImage` to hook `hvloader.dll!HvlLaunchHypervisor`.
- Inserts an identity-mapped PML4 entry into Hyper-V's address space.
- Hooks the VM exit entry routine in the finalized, SLAT-protected Hyper-V image before executing the original hypervisor entry point.

### 3. Hypervisor Execution (`hyperv-attachment`)
- Executes custom VM exit dispatchers and IDT handlers.
- Configures APIC (xAPIC / x2APIC) for Inter-Processor Interrupt (IPI) NMIs to synchronize SLAT page table caches across all host logical processors.
- Re-maps guest SLAT entries for internal heap pages to dummy frames, hiding hypervisor structures from guest physical memory scans.

---

## SLAT Code Hooking Mechanism

### Intel (EPT)
- Sets target page permissions to Execute-Only (`--x`).
- Configures Page Frame Number (PFN) to point to the shadow execution page containing the detour hook.
- Read/Write attempts by the guest trigger EPT Violations, dynamically swapping the PFN to the original page (`rw-`).
- Subsequent instruction fetches swap execution back to the shadow page (`--x`).

### AMD (NPT)
- Maintains a dual Nested Page Table structure:
  - **Hyper-V Nested CR3**: Standard guest execution, target hooked pages set to Non-Executable (`rw-`).
  - **Hook Nested CR3**: Shadow execution identity map where only hooked pages are marked Executable (`--x`) with shadow PFNs.
- Execution of a hooked page under Hyper-V Nested CR3 triggers a Nested Page Fault (NPF), switching CR3 to the Hook Nested CR3.
- Returning to non-hooked code switches execution back to the primary Hyper-V Nested CR3.

### Cache Synchronization
- Uses APIC ICR (Interrupt Command Register) to broadcast NMIs to host processors.
- Logical processors flush local TLB / SLAT caches upon receiving the NMI signal.

---

## Usermode Debugger Interface

The usermode application (`usermode`) communicates with the hypervisor via CPUID-based hypercalls, allowing kernel debugging and memory inspection without standard Windows Debug API artifacts.

### Command Reference

| Command | Description | Syntax |
| :--- | :--- | :--- |
| `rgpm` | Read Guest Physical Memory | `rgpm <gpa> <size>` |
| `wgpm` | Write Guest Physical Memory | `wgpm <gpa> <value> <size>` |
| `cgpm` | Copy Guest Physical Memory | `cgpm <dst_gpa> <src_gpa> <size>` |
| `gvat` | Translate GVA to GPA | `gvat <gva> <cr3>` |
| `rgvm` | Read Guest Virtual Memory | `rgvm <gva> <cr3> <size>` |
| `wgvm` | Write Guest Virtual Memory | `wgvm <gva> <cr3> <value> <size>` |
| `cgvm` | Copy Guest Virtual Memory | `cgvm <dst_gva> <dst_cr3> <src_gva> <src_cr3> <size>` |
| `akh` | Add Kernel Hook | `akh <gva> [--asmbytes ...] [--post_original_asmbytes ...] [--monitor]` |
| `rkh` | Remove Kernel Hook | `rkh <gva>` |
| `hgpp` | Hide Guest Physical Page | `hgpp <gpa>` |
| `fl` | Flush Trap Frame Logs | `fl` |
| `lkm` | List Loaded Kernel Modules | `lkm` |
| `kme` | List Kernel Module Exports | `kme <module_name>` |
| `dkm` | Dump Kernel Module to File | `dkm <module_name> <out_dir>` |

---

## Building and Deployment

### Prerequisites
- **Visual Studio 2022** (C++ Desktop & Driver Toolsets)
- **NASM** (Netwide Assembler) with `NASM_PREFIX` added to Environment Variables
- **Git**

### 1. Clone Submodules
```bash
git clone --branch OpenSSL_1_1_0-stable https://github.com/openssl/openssl.git uefi-boot/ext/openssl
git clone https://github.com/ionescu007/edk2.git uefi-boot/ext/edk2/src
```

### 2. Build EDK2 Dependency
Open `uefi-boot\ext\edk2\build\EDK-II.sln` in Visual Studio and build the solution for `x64`.

### 3. Build hyper-reV
1. Open `hyper-reV.sln`.
2. Configure architecture target in `hyperv-attachment/src/arch_config.h`:
   - For Intel: `#define _INTELMACHINE`
   - For AMD: Comment out `#define _INTELMACHINE`
3. Build the Solution (`Release | x64`).

### 4. Deployment
Run `load-hyper-reV.bat` as Administrator in the output directory containing `uefi-boot.efi` and `hyperv-attachment.dll`. Reboot the system to activate the hypervisor introspection layer.

---

## Supported Environments

Tested on Intel Core & AMD Ryzen processors:
- Windows 10 (21H2, 22H2)
- Windows 11 (22H2, 23H2, 24H2)

*Requires Hyper-V feature to be enabled in Windows.*

---

## License

This project is licensed under the GPL-3.0 License - see the [LICENSE](LICENSE) file for details.
