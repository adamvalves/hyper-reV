#include "arch.h"

#include <intrin.h>

#include "../memory_manager/memory_manager.h"
#include "../crt/crt.h"

#ifdef _INTELMACHINE
#include <intrin.h>
#include <ia32-doc/ia32.hpp>

std::uint64_t get_vmexit_instruction_length()
{
#ifdef _INTELMACHINE
	std::uint64_t vmexit_instruction_length = 0;

	__vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH, &vmexit_instruction_length);

	return vmexit_instruction_length;
#else
	vmcb_t* const vmcb = get_vmcb();

	return vmcb->control.next_rip - vmcb->save_state.rip;
#endif
}

#else
std::uint8_t get_vmcb_routine_bytes[27];

typedef vmcb_t*(*get_vmcb_routine_t)();

vmcb_t* arch::get_vmcb()
{
	get_vmcb_routine_t get_vmcb_routine = reinterpret_cast<get_vmcb_routine_t>(&get_vmcb_routine_bytes[0]);

	return get_vmcb_routine();
}

void arch::parse_vmcb_gadget(const std::uint8_t* get_vmcb_gadget)
{
	constexpr std::uint32_t final_needed_opcode_offset = 23;

	crt::copy_memory(&get_vmcb_routine_bytes[0], get_vmcb_gadget, final_needed_opcode_offset);

	if (get_vmcb_gadget[25] == 8) // needs to be dereffed once more
	{
		constexpr std::uint8_t return_bytes[4] = {
			0x48, 0x8B, 0x00, // mov rax, [rax]
			0xC3 // ret
		};

		crt::copy_memory(&get_vmcb_routine_bytes[final_needed_opcode_offset], &return_bytes[0], sizeof(return_bytes));
	}
	else
	{
		get_vmcb_routine_bytes[final_needed_opcode_offset] = 0xC3;
	}
}
#endif

std::uint64_t arch::get_vmexit_reason()
{
#ifdef _INTELMACHINE
	std::uint64_t vmexit_reason = 0;

	__vmx_vmread(VMCS_EXIT_REASON, &vmexit_reason);

	return vmexit_reason;
#else
	vmcb_t* const vmcb = get_vmcb();

	return vmcb->control.vmexit_reason;
#endif
}

std::uint8_t arch::is_cpuid(std::uint64_t vmexit_reason)
{
#ifdef _INTELMACHINE
	return vmexit_reason == VMX_EXIT_REASON_EXECUTE_CPUID;
#else
	return vmexit_reason == SVM_EXIT_REASON_CPUID;
#endif
}

std::uint8_t arch::is_slat_violation(std::uint64_t vmexit_reason)
{
#ifdef _INTELMACHINE
	return vmexit_reason == VMX_EXIT_REASON_EPT_VIOLATION;
#else
	return vmexit_reason == SVM_EXIT_REASON_NPF;
#endif
}

std::uint8_t arch::is_non_maskable_interrupt_exit(std::uint64_t vmexit_reason)
{
#ifdef _INTELMACHINE
	if (vmexit_reason != VMX_EXIT_REASON_EXCEPTION_OR_NMI)
	{
		return 0;
	}

	std::uint64_t raw_interruption_information = 0;

	__vmx_vmread(VMCS_VMEXIT_INTERRUPTION_INFORMATION, &raw_interruption_information);

	vmexit_interrupt_information interrupt_information = { .flags = static_cast<std::uint32_t>(raw_interruption_information) };

	return interrupt_information.interruption_type == interruption_type::non_maskable_interrupt;
#else
	return vmexit_reason == SVM_EXIT_REASON_PHYSICAL_NMI;
#endif
}

cr3 arch::get_guest_cr3()
{
	cr3 guest_cr3 = { };

#ifdef _INTELMACHINE
	__vmx_vmread(VMCS_GUEST_CR3, &guest_cr3.flags);
#else
	vmcb_t* const vmcb = get_vmcb();

	guest_cr3.flags = vmcb->save_state.cr3;
#endif

	return guest_cr3;
}

std::uint64_t arch::get_guest_rip()
{
#ifdef _INTELMACHINE
	std::uint64_t guest_rip = 0;

	__vmx_vmread(VMCS_GUEST_RIP, &guest_rip);

	return guest_rip;
#else
	vmcb_t* const vmcb = get_vmcb();

	return vmcb->save_state.rip;
#endif
}

void arch::set_guest_rip(std::uint64_t guest_rip)
{
#ifdef _INTELMACHINE
	__vmx_vmwrite(VMCS_GUEST_RIP, guest_rip);
#else
	vmcb_t* const vmcb = get_vmcb();

	vmcb->save_state.rip = guest_rip;
#endif
}

void arch::advance_guest_rip()
{
#ifdef _INTELMACHINE
	std::uint64_t guest_rip = get_guest_rip();

	std::uint64_t instruction_length = get_vmexit_instruction_length();

	std::uint64_t next_rip = guest_rip + instruction_length;
#else
	vmcb_t* const vmcb = get_vmcb();

	std::uint64_t next_rip = vmcb->control.next_rip;
#endif

	set_guest_rip(next_rip);
}
