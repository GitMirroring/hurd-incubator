#ifdef __ASSEMBLY__

#include <asm/dwarf2.h>

/* The annotation hides the frame from the unwinder and makes it look
   like a ordinary ebp save/restore. This avoids some special cases for
   frame pointer later */
#ifdef CONFIG_FRAME_POINTER
#ifdef __x86_64__
	.macro FRAME
	pushq %rbp
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rbp,0
	movq %rsp,%rbp
	.endm
	.macro ENDFRAME
	popq %rbp
	CFI_ADJUST_CFA_OFFSET -8
	CFI_RESTORE rbp
	.endm
#else
	.macro FRAME
	pushl %ebp
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebp,0
	movl %esp,%ebp
	.endm
	.macro ENDFRAME
	popl %ebp
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ebp
	.endm
#endif
#else
	.macro FRAME
	.endm
	.macro ENDFRAME
	.endm
#endif

#endif  /*  __ASSEMBLY__  */
