/*
 * Guest virtual memory / address-space hooks (M2 execve child AS).
 */
int guest_vmm_exec_child_as(void *entry, void *stack)
{
	(void)entry;
	(void)stack;
	return -38;
}
