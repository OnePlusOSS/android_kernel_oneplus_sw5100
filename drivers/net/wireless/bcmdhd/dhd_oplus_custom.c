#include <linux/module.h>
#include <linux/errno.h>

#define BCMDHD_WLAN_MAC_STR 6
static char brcm_mac_addr[BCMDHD_WLAN_MAC_STR];
static bool bcm_mac_parse_completed = false;

#define DHD_MAC_USE_MODULE_PARAM 1

#if DHD_MAC_USE_MODULE_PARAM
#ifndef MOD_PARAM_PATHLEN
#define MOD_PARAM_PATHLEN		(1024)
#endif

char dhd_mac[MOD_PARAM_PATHLEN];
module_param_string(dhd_mac, dhd_mac, MOD_PARAM_PATHLEN, 0660);

#endif

static int dhd_mac_addr_setup(char *str)
{
	char macstr[BCMDHD_WLAN_MAC_STR*3];
	char *macptr = macstr;
	char *token;
	int i = 0;

	pr_info("%s enter\n", __FUNCTION__);
	if (!str || strlen(str) == 0)
		return -EPERM;

	if (strlen(str) >= sizeof(macstr))
		return -EPERM;

	strlcpy(macstr, str, sizeof(macstr));

	while (((token = strsep(&macptr, ":")) != NULL) && (i < BCMDHD_WLAN_MAC_STR)) {
		unsigned long val;
		int res;

		res = kstrtoul(token, 0x10, &val);
		if (res < 0)
			break;
		brcm_mac_addr[i++] = (u8)val;
	}

	if (i < BCMDHD_WLAN_MAC_STR && strlen(macstr) == BCMDHD_WLAN_MAC_STR*2) {
		// try again with wrong format (sans colons)
		u64 mac;
		if (kstrtoull(macstr, 0x10, &mac) < 0)
			return -EINVAL;

		for (i = 0; i < BCMDHD_WLAN_MAC_STR; i++)
			brcm_mac_addr[BCMDHD_WLAN_MAC_STR-1-i] = (u8)((0xFF)&(mac>>(i*8)));
	}

	bcm_mac_parse_completed = true;

	pr_info("%s exit\n", __FUNCTION__);
	return 0;
}

int dhd_get_factory_mac(char *buf, int len)
{
	if (len < BCMDHD_WLAN_MAC_STR)
		return -EINVAL;

	if (!bcm_mac_parse_completed)
		return -EINVAL;

	memcpy(buf, brcm_mac_addr, BCMDHD_WLAN_MAC_STR);
	return 0;
}

static int dhd_oplus_mac_init(void)
{
#if DHD_MAC_USE_MODULE_PARAM
	return dhd_mac_addr_setup(dhd_mac);
#else
	return -EINVAL;
#endif
}

int dhd_oplus_custom_init(void)
{
	int ret;
	ret = dhd_oplus_mac_init();
	if (ret) {
		pr_warn("mac init fail\n");
	}
	return ret;
}
