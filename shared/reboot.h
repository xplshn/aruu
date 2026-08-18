/*
 * magic values required to use _reboot() system call
 */

#define LINUX_REBOOT_MAGIC1  0xfee1dead
#define LINUX_REBOOT_MAGIC2  672274793
#define LINUX_REBOOT_MAGIC2A 85072278
#define LINUX_REBOOT_MAGIC2B 369367448
#define LINUX_REBOOT_MAGIC2C 537993216

/*
 * commands accepted by the _reboot() system call
 *
 * RESTART restart system using default command and mode
 * HALT stop OS and give system control to ROM monitor, if any
 * CAD_ON ctrl-alt-del sequence causes RESTART command
 * CAD_OFF ctrl-alt-del sequence sends SIGINT to init task
 * POWER_OFF stop OS and remove all power from system, if possible
 * RESTART2 restart system using given command string
 * SW_SUSPEND suspend system using software suspend if compiled in
 * KEXEC restart system using a previously loaded linux kernel
 */

#define LINUX_REBOOT_CMD_RESTART    0x01234567
#define LINUX_REBOOT_CMD_HALT       0xCDEF0123
#define LINUX_REBOOT_CMD_CAD_ON     0x89ABCDEF
#define LINUX_REBOOT_CMD_CAD_OFF    0x00000000
#define LINUX_REBOOT_CMD_POWER_OFF  0x4321FEDC
#define LINUX_REBOOT_CMD_RESTART2   0xA1B2C3D4
#define LINUX_REBOOT_CMD_SW_SUSPEND 0xD000FCE2
#define LINUX_REBOOT_CMD_KEXEC      0x45584543
