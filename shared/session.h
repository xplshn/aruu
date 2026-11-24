/* see LICENSE file for copyright and license details */
#ifndef ARUU_SESSION_H
#define ARUU_SESSION_H

/* a backend per system: the record of a login on the controlling tty
 * that who and last read, best effort */
void session_record(const char *user);

#endif
