const struct {
	const char *name;
	unsigned int stepid;
} slurm_stepid_mapping[] = {
	{ "batch", SLURM_BATCH_SCRIPT },
	{ "interactive", SLURM_INTERACTIVE_STEP },
	{ "extern", SLURM_EXTERN_CONT },
	{ "TBD", SLURM_PENDING_STEP },
	{ NULL, 0 }
};
