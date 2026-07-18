/*
 * Cooperative single-child process model (M2 partial).
 *
 * vfork → execve → exit → wait4 is implemented in the local work tree.
 * This stub documents the ENOSYS policy for fork / clone / waitid.
 */
int sys_vfork(void)
{
	return -38; /* ENOSYS until wired to local scheduler */
}

int sys_execve(const char *path, char *const argv[], char *const envp[])
{
	(void)path;
	(void)argv;
	(void)envp;
	return -38;
}

int sys_wait4(int pid, int *status, int options, void *rusage)
{
	(void)pid;
	(void)status;
	(void)options;
	(void)rusage;
	return -38;
}
