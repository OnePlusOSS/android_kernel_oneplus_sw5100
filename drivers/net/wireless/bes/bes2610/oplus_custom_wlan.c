#include <linux/module.h>
#include <linux/errno.h>

#define OPLUS_WLAN_MAC_STR 6
static char oplus_mac_addr[OPLUS_WLAN_MAC_STR];
static bool oplus_mac_parse_completed = false;

#define OPLUS_MAC_USE_MODULE_PARAM 1


#if OPLUS_MAC_USE_MODULE_PARAM
#ifndef MOD_PARAM_PATHLEN
#define MOD_PARAM_PATHLEN		(1024)
#endif

char oplus_wlan_mac[MOD_PARAM_PATHLEN];
module_param_string(oplus_wlan_mac, oplus_wlan_mac, MOD_PARAM_PATHLEN, 0660);

#elif IS_ENABLED(CONFIG_OPLUS_FEATURE_CMDLINE)
extern char oplus_wlan_mac_str[];
#endif

static int oplus_wlan_mac_addr_setup(char *str)
{
	char macstr[OPLUS_WLAN_MAC_STR*3];
	char *macptr = macstr;
	char *token;
	int i = 0;

	pr_info("%s enter\n", __FUNCTION__);
	if (!str || strlen(str) == 0)
		return -EPERM;

	if (strlen(str) >= sizeof(macstr))
		return -EPERM;

	strlcpy(macstr, str, sizeof(macstr));

	while (((token = strsep(&macptr, ":")) != NULL) && (i < OPLUS_WLAN_MAC_STR)) {
		unsigned long val;
		int res;

		res = kstrtoul(token, 0x10, &val);
		if (res < 0)
			break;
		oplus_mac_addr[i++] = (u8)val;
	}

	if (i < OPLUS_WLAN_MAC_STR && strlen(macstr) == OPLUS_WLAN_MAC_STR*2) {
		// try again with wrong format (sans colons)
		u64 mac;
		if (kstrtoull(macstr, 0x10, &mac) < 0)
			return -EINVAL;

		for (i = 0; i < OPLUS_WLAN_MAC_STR; i++)
			oplus_mac_addr[OPLUS_WLAN_MAC_STR-1-i] = (u8)((0xFF)&(mac>>(i*8)));
	}

	oplus_mac_parse_completed = true;

	pr_info("%s exit\n", __FUNCTION__);
	return 0;
}

int oplus_wlan_get_factory_mac(char *buf, int len)
{
	if (len < OPLUS_WLAN_MAC_STR)
		return -EINVAL;

	if (!oplus_mac_parse_completed)
		return -EINVAL;

	memcpy(buf, oplus_mac_addr, OPLUS_WLAN_MAC_STR);
	return 0;
}

static int oplus_wlan_mac_init(void)
{
#if OPLUS_MAC_USE_MODULE_PARAM
	return oplus_wlan_mac_addr_setup(oplus_wlan_mac);
#elif IS_ENABLED(CONFIG_OPLUS_FEATURE_CMDLINE)
	return oplus_wlan_mac_addr_setup(oplus_wlan_mac_str);
#else
	return -EINVAL;
#endif
}

int oplus_custom_init(void)
{
	int ret;
	ret = oplus_wlan_mac_init();
	if (ret) {
		pr_warn("mac init fail\n");
	}
	return ret;
}
