#ifndef __OPLUS_CUSTOM_WLAN__H__
#define __OPLUS_CUSTOM_WLAN__H__

#include <linux/types.h>

int oplus_wlan_get_factory_mac(char *buf, int len);

int oplus_custom_init(void);

#define OPLUS_CONNECT_SCAN_DUR  40000
#define OPLUSMACDBG				"%02x:xx:xx:xx:x%x:%02x"
#define OPLUSMAC2STRDBG(ea)	((const u8*)(ea))[0], \
			(((const u8*)(ea))[4] & 0xf), \
			((const u8*)(ea))[5]

#endif /*end of __OPLUS_CUSTOM_WLAN__H__*/
