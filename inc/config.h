#ifndef NAKD_CONFIG_H
#define NAKD_CONFIG_H

void nakd_config_init(void);
void nakd_config_cleanup(void);

int nakd_config_key(const char *key, char **ret);

#endif
