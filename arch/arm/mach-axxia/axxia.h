#ifndef _AXXIA_H

void axxia_init_clocks(int is_sim);
void axxia_ddr_retention_init(void);

extern struct smp_operations axxia_smp_ops;

#endif
