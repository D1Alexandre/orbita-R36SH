#ifndef ORBITA_PORT_LINUX_H
#define ORBITA_PORT_LINUX_H

/* Initialize before starting workers. NULL uses XDG_DATA_HOME or HOME. */
int orb_linux_init(const char *data_dir);
void orb_linux_shutdown(void);
const char *orb_linux_data_dir(void);
void orb_linux_cancel(int cancel);

#endif
