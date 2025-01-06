/*
 * @file bes_common.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_COMMON_
#define _BES_COMMON_

#define IP_PROT_ICMP		0x1	/* ICMP protocol */
#define IP_PROT_IGMP		0x2	/* IGMP protocol */
#define IP_PROT_TCP		0x6	/* TCP protocol */
#define IP_PROT_UDP		0x11	/* UDP protocol type */
#define IP_PROT_GRE		0x2f	/* GRE protocol type */
#define IP_PROT_ICMP6		0x3a	/* ICMPv6 protocol type */

#define ICMP_ECHOREPLY		0	/* Echo Reply			*/
#define ICMP_DEST_UNREACH	3	/* Destination Unreachable	*/
#define ICMP_SOURCE_QUENCH	4	/* Source Quench		*/
#define ICMP_REDIRECT		5	/* Redirect (change route)	*/
#define ICMP_ECHO		8	/* Echo Request			*/

#define WPA_CAPABILITY_PREAUTH BIT(0)

#define ENCRYP_MODE_CHARACTER_SIZE  50

#define RSN_SELECTOR_PUT(a, val) WPA_PUT_BE32((u8 *) (a), (val))
#define RSN_SELECTOR_GET(a) WPA_GET_BE32((const u8 *) (a))

#define WPA_AUTH_KEY_MGMT_NONE RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x50, 0xf2, 1)
#define WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#define WPA_AUTH_KEY_MGMT_CCKM RSN_SELECTOR(0x00, 0x40, 0x96, 0)
#define WPA_CIPHER_SUITE_NONE RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_CIPHER_SUITE_TKIP RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#define WPA_CIPHER_SUITE_CCMP RSN_SELECTOR(0x00, 0x50, 0xf2, 4)

#define WPA_SELECTOR_LEN 4
#define WPA_VERSION 1
#define RSN_SELECTOR_LEN 4
#define RSN_VERSION 1

#define RSN_SELECTOR(a, b, c, d) \
	((((u32) (a)) << 24) | (((u32) (b)) << 16) | (((u32) (c)) << 8) | \
	 (u32) (d))

/* protocol definition */
#define WPA_PROTO_WPA BIT(0)
#define WPA_PROTO_RSN BIT(1)
#define WPA_PROTO_WAPI BIT(2)
#define WPA_PROTO_OSEN BIT(3)

#define WPA_CIPHER_NONE BIT(0)
#define WPA_CIPHER_WEP40 BIT(1)
#define WPA_CIPHER_WEP104 BIT(2)
#define WPA_CIPHER_TKIP BIT(3)
#define WPA_CIPHER_CCMP BIT(4)
#define WPA_CIPHER_AES_128_CMAC BIT(5)
#define WPA_CIPHER_GCMP BIT(6)
#define WPA_CIPHER_SMS4 BIT(7)
#define WPA_CIPHER_GCMP_256 BIT(8)
#define WPA_CIPHER_CCMP_256 BIT(9)
#define WPA_CIPHER_BIP_GMAC_128 BIT(11)
#define WPA_CIPHER_BIP_GMAC_256 BIT(12)
#define WPA_CIPHER_BIP_CMAC_256 BIT(13)
#define WPA_CIPHER_GTK_NOT_USED BIT(14)

#define WPA_KEY_MGMT_IEEE8021X BIT(0)
#define WPA_KEY_MGMT_PSK BIT(1)
#define WPA_KEY_MGMT_NONE BIT(2)
#define WPA_KEY_MGMT_IEEE8021X_NO_WPA BIT(3)
#define WPA_KEY_MGMT_WPA_NONE BIT(4)
#define WPA_KEY_MGMT_FT_IEEE8021X BIT(5)
#define WPA_KEY_MGMT_FT_PSK BIT(6)
#define WPA_KEY_MGMT_IEEE8021X_SHA256 BIT(7)
#define WPA_KEY_MGMT_PSK_SHA256 BIT(8)
#define WPA_KEY_MGMT_WPS BIT(9)
#define WPA_KEY_MGMT_SAE BIT(10)
#define WPA_KEY_MGMT_FT_SAE BIT(11)
#define WPA_KEY_MGMT_WAPI_PSK BIT(12)
#define WPA_KEY_MGMT_WAPI_CERT BIT(13)
#define WPA_KEY_MGMT_CCKM BIT(14)
#define WPA_KEY_MGMT_OSEN BIT(15)
#define WPA_KEY_MGMT_IEEE8021X_SUITE_B BIT(16)
#define WPA_KEY_MGMT_IEEE8021X_SUITE_B_192 BIT(17)
#define WPA_KEY_MGMT_FILS_SHA256 BIT(18)
#define WPA_KEY_MGMT_FILS_SHA384 BIT(19)
#define WPA_KEY_MGMT_FT_FILS_SHA256 BIT(20)
#define WPA_KEY_MGMT_FT_FILS_SHA384 BIT(21)
#define WPA_KEY_MGMT_OWE BIT(22)
#define WPA_KEY_MGMT_DPP BIT(23)
#define WPA_KEY_MGMT_FT_IEEE8021X_SHA384 BIT(24)

#define WPA_KEY_MGMT_FT (WPA_KEY_MGMT_FT_PSK | \
			 WPA_KEY_MGMT_FT_IEEE8021X | \
			 WPA_KEY_MGMT_FT_IEEE8021X_SHA384 | \
			 WPA_KEY_MGMT_FT_SAE | \
			 WPA_KEY_MGMT_FT_FILS_SHA256 | \
			 WPA_KEY_MGMT_FT_FILS_SHA384)

#define RSN_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#define RSN_AUTH_KEY_MGMT_FT_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#define RSN_AUTH_KEY_MGMT_FT_PSK RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#define RSN_AUTH_KEY_MGMT_802_1X_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_AUTH_KEY_MGMT_PSK_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_AUTH_KEY_MGMT_TPK_HANDSHAKE RSN_SELECTOR(0x00, 0x0f, 0xac, 7)
#define RSN_AUTH_KEY_MGMT_SAE RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#define RSN_AUTH_KEY_MGMT_FT_SAE RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#define RSN_AUTH_KEY_MGMT_802_1X_SUITE_B RSN_SELECTOR(0x00, 0x0f, 0xac, 11)
#define RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192 RSN_SELECTOR(0x00, 0x0f, 0xac, 12)
#define RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384 RSN_SELECTOR(0x00, 0x0f, 0xac, 13)
#define RSN_AUTH_KEY_MGMT_FILS_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 14)
#define RSN_AUTH_KEY_MGMT_FILS_SHA384 RSN_SELECTOR(0x00, 0x0f, 0xac, 15)
#define RSN_AUTH_KEY_MGMT_FT_FILS_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 16)
#define RSN_AUTH_KEY_MGMT_FT_FILS_SHA384 RSN_SELECTOR(0x00, 0x0f, 0xac, 17)
#define RSN_AUTH_KEY_MGMT_OWE RSN_SELECTOR(0x00, 0x0f, 0xac, 18)
#define RSN_AUTH_KEY_MGMT_CCKM RSN_SELECTOR(0x00, 0x40, 0x96, 0x00)
#define RSN_AUTH_KEY_MGMT_OSEN RSN_SELECTOR(0x50, 0x6f, 0x9a, 0x01)
#define RSN_AUTH_KEY_MGMT_DPP RSN_SELECTOR(0x50, 0x6f, 0x9a, 0x02)

#define RSN_CIPHER_SUITE_NONE RSN_SELECTOR(0x00, 0x0f, 0xac, 0)
#define RSN_CIPHER_SUITE_WEP40 RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_CIPHER_SUITE_TKIP RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#define RSN_CIPHER_SUITE_CCMP RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#define RSN_CIPHER_SUITE_WEP104 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_CIPHER_SUITE_AES_128_CMAC RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED RSN_SELECTOR(0x00, 0x0f, 0xac, 7)
#define RSN_CIPHER_SUITE_GCMP RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#define RSN_CIPHER_SUITE_GCMP_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#define RSN_CIPHER_SUITE_CCMP_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 10)
#define RSN_CIPHER_SUITE_BIP_GMAC_128 RSN_SELECTOR(0x00, 0x0f, 0xac, 11)
#define RSN_CIPHER_SUITE_BIP_GMAC_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 12)
#define RSN_CIPHER_SUITE_BIP_CMAC_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 13)
#define RSN_CIPHER_SUITE_SMS4 RSN_SELECTOR(0x00, 0x14, 0x72, 1)
#define RSN_CIPHER_SUITE_CKIP RSN_SELECTOR(0x00, 0x40, 0x96, 0)
#define RSN_CIPHER_SUITE_CKIP_CMIC RSN_SELECTOR(0x00, 0x40, 0x96, 1)
#define RSN_CIPHER_SUITE_CMIC RSN_SELECTOR(0x00, 0x40, 0x96, 2)
/* KRK is defined for nl80211 use only */
#define RSN_CIPHER_SUITE_KRK RSN_SELECTOR(0x00, 0x40, 0x96, 255)

#define WPA_OUI_TYPE RSN_SELECTOR(0x00, 0x50, 0xf2, 1)

#define OSEN_IE_VENDOR_TYPE 0x506f9a12

/* IEEE 802.11i */
#define PMKID_LEN 16
#define PMK_LEN 32
#define PMK_LEN_SUITE_B_192 48
#define PMK_LEN_MAX 64
#define WPA_REPLAY_COUNTER_LEN 8
#define WPA_NONCE_LEN 32
#define WPA_KEY_RSC_LEN 8
#define WPA_GMK_LEN 32
#define WPA_GTK_MAX_LEN 32

/* IEEE 802.11, 8.5.2 EAPOL-Key frames */
#define WPA_KEY_INFO_TYPE_MASK ((u16) (BIT(0) | BIT(1) | BIT(2)))
#define WPA_KEY_INFO_TYPE_AKM_DEFINED 0
#define WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 BIT(0)
#define WPA_KEY_INFO_TYPE_HMAC_SHA1_AES BIT(1)
#define WPA_KEY_INFO_TYPE_AES_128_CMAC 3
#define WPA_KEY_INFO_KEY_TYPE BIT(3) /* 1 = Pairwise, 0 = Group key */
/* bit4..5 is used in WPA, but is reserved in IEEE 802.11i/RSN */
#define WPA_KEY_INFO_KEY_INDEX_MASK (BIT(4) | BIT(5))
#define WPA_KEY_INFO_KEY_INDEX_SHIFT 4
#define WPA_KEY_INFO_INSTALL BIT(6) /* pairwise */
#define WPA_KEY_INFO_TXRX BIT(6) /* group */
#define WPA_KEY_INFO_ACK BIT(7)
#define WPA_KEY_INFO_MIC BIT(8)
#define WPA_KEY_INFO_SECURE BIT(9)
#define WPA_KEY_INFO_ERROR BIT(10)
#define WPA_KEY_INFO_REQUEST BIT(11)
#define WPA_KEY_INFO_ENCR_KEY_DATA BIT(12) /* IEEE 802.11i/RSN only */
#define WPA_KEY_INFO_SMK_MESSAGE BIT(13)

#define EAPOL_VERSION 2

#define SHA256_MAC_LEN 	32
#define SHA384_MAC_LEN	48
#define SHA512_MAC_LEN 	64

/* entended */
/* IEEE 802.11b */
#define WLAN_STATUS_ASSOC_DENIED_NOSHORT 19
#define WLAN_STATUS_ASSOC_DENIED_NOPBCC 20
#define WLAN_STATUS_ASSOC_DENIED_NOAGILITY 21
/* IEEE 802.11h */
#define WLAN_STATUS_SPEC_MGMT_REQUIRED 22
#define WLAN_STATUS_PWR_CAPABILITY_NOT_VALID 23
#define WLAN_STATUS_SUPPORTED_CHANNEL_NOT_VALID 24
/* IEEE 802.11g */
#define WLAN_STATUS_ASSOC_DENIED_NO_SHORT_SLOT_TIME 25
#define WLAN_STATUS_ASSOC_DENIED_NO_ER_PBCC 26
#define WLAN_STATUS_ASSOC_DENIED_NO_DSSS_OFDM 27
/* IEEE 802.11w */
#define WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY 30
#define WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION 31
/* IEEE 802.11i */
#define WLAN_STATUS_INVALID_IE 40
#define WLAN_STATUS_GROUP_CIPHER_NOT_VALID 41
#define WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID 42
#define WLAN_STATUS_AKMP_NOT_VALID 43
#define WLAN_STATUS_UNSUPPORTED_RSN_IE_VERSION 44
#define WLAN_STATUS_INVALID_RSN_IE_CAPAB 45
#define WLAN_STATUS_CIPHER_REJECTED_PER_POLICY 46
#define WLAN_STATUS_TS_NOT_CREATED 47
#define WLAN_STATUS_DIRECT_LINK_NOT_ALLOWED 48
#define WLAN_STATUS_DEST_STA_NOT_PRESENT 49
#define WLAN_STATUS_DEST_STA_NOT_QOS_STA 50
#define WLAN_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE 51
/* IEEE 802.11r */
#define WLAN_STATUS_INVALID_FT_ACTION_FRAME_COUNT 52
#define WLAN_STATUS_INVALID_PMKID 53
#define WLAN_STATUS_INVALID_MDIE 54
#define WLAN_STATUS_INVALID_FTIE 55

enum {
	EAPOL_KEY_TYPE_RC4 = 1,
	EAPOL_KEY_TYPE_RSN = 2,
	EAPOL_KEY_TYPE_WPA = 254
};

enum mic_alg {
	MIC_ALG_MD5 = 0,
	MIC_ALG_AES_CMAC,
	MIC_ALG_SHA1,
	MIC_ALG_SHA256,
	MIC_ALG_SHA384,
	MIC_ALG_SHA512,
	MIC_ALG_AES_SIV_256,
	MIC_ALG_AES_SIV_512
};

enum dec_alg {
	DEC_ALG_RC4 = 0,
	DEC_ALG_NIST_AES,
	DEC_ALG_AES_SIV_256,
	DEC_ALG_AES_SIV_512
};

struct rsn_ie_hdr {
	u8 elem_id; /* WLAN_EID_RSN */
	u8 len;
	u8 version[2]; /* little endian */
} __packed;

struct wpa_ie_hdr {
	u8 elem_id;
	u8 len;
	u8 oui[4]; /* 24-bit OUI followed by 8-bit OUI type */
	u8 version[2]; /* little endian */
} __packed;

struct icmphdr {
	__u8 type;
	__u8 code;
	__sum16 checksum;
	union {
		struct {
			__be16 id;
			__be16 sequence;
		} echo;
		__be32 gateway;
		struct {
			__be16 __unused;
			__be16 mtu;
		} frag;
		__u8 reserved[4];
	} un;
};

struct icmpv6_option_lla {
	u8 type;
	u8 len;
	u8 addr[ETH_ALEN];
};

struct wpa_ie_data {
	int proto;
	int pairwise_cipher;
	int has_pairwise;
	int group_cipher;
	int has_group;
	int key_mgmt;
	int capabilities;
	size_t num_pmkid;
	const u8 *pmkid;
	int mgmt_group_cipher;
	u16 group;
};

/* DHCP Message types (values for TAG_DHCP_MESSAGE option) */
#define DHCPDISCOVER		1
#define DHCPOFFER		2
#define DHCPREQUEST		3
#define DHCPACK			5
#define DHCPNAK			6
#define DHCPRELEASE		7

#define DHCP_MAGIC		0x63825363

/* DHCP Option type */
#define DHCP_OPTION_PADDING	0x00
#define DHCP_OPTION_SUBNET_MASK	0x01
#define DHCP_OPTION_ROUTER	0x03
#define DHCP_OPTION_DNS_SERVER	0x06
#define DHCP_OPTION_HOST_NAME	0x0c
#define DHCP_OPTION_BROADCAST	0x1c
#define DHCP_OPTION_REQ_IPADDR	0x32
#define DHCP_OPTION_LEASE_TIME	0x33
#define DHCP_OPTION_OVERLOAD	0x34
#define DHCP_OPTION_MSG_TYPE	0x35
#define DHCP_OPTION_SERVER_ID	0x36
#define DHCP_OPTION_REQ_LIST	0x37
#define DHCP_OPTION_MAX_SIZE	0x39
#define DHCP_OPTION_CLIENTID	0x3D
#define DHCP_OPTION_VENDOR	0x3C
#define DHCP_OPTION_FQDN	0x51
#define DHCP_OPTION_END		0xFF

#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68

/* DHCP message type */
struct dhcp_message {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 ciaddr;
	u32 yiaddr;
	u32 siaddr;
	u32 giaddr;
	u8 chaddr[16];
	u8 sname[64];
	u8 file[128];
	u32 cookie;
	u8 options[1]; /* 312 - cookie */
};

/* EAPOL-Key field size */
#define WPA_KEY_REPLAY_LEN	8
#define WPA_KEY_NONCE_LEN	32
#define WPA_KEY_IV_LEN		16
#define WPA_KEY_RSC_LEN		8
#define WPA_KEY_ID_LEN		8
#define WPA_KEY_MIC_LEN		16

/* EAPOL-Key types */
#define EAPOL_RC4_KEY		1	/* WEP */
#define EAPOL_WPA2_KEY		2	/* 802.11i/WPA2 */
#define EAPOL_WPA_KEY		254	/* WPA */

/* EAPOL Key information bit(network byte order) */
#define KEYINFO_TYPE_MASK	(1 << 3)
#define KEYINFO_INSTALL_MASK	(1 << 6)
#define KEYINFO_KEYACK_MASK	(1 << 7)
#define KEYINFO_KEYMIC_MASK	(1 << 8)
#define KEYINFO_SECURE_MASK	(1 << 9)
#define KEYINFO_ERROR_MASK	(1 << 10)
#define KEYINFO_REQ_MASK	(1 << 11)

enum msg_eapol_t {
	EAPOL_OTHER = 0,
	EAPOL_4WAY_M1,
	EAPOL_4WAY_M2,
	EAPOL_4WAY_M3,
	EAPOL_4WAY_M4,
	EAPOL_GROUPKEY_M1,
	EAPOL_GROUPKEY_M2
} ;

/* EAPOL header */
struct eapol_hdr {
	u8 version;				/* EAPOL protocol version */
	u8 type;				/* EAPOL type */
	u16 length;				/* Length of body */
	u8 key_type;				/* Key Descriptor Type */
	u16 key_info;				/* Key Information (unaligned) */
	u16 key_len;				/* Key Length (unaligned) */
	u8 replay[WPA_KEY_REPLAY_LEN];		/* Replay Counter */
	u8 nonce[WPA_KEY_NONCE_LEN];		/* Nonce */
	u8 iv[WPA_KEY_IV_LEN];			/* Key IV */
	u8 rsc[WPA_KEY_RSC_LEN];		/* Key RSC */
	u8 id[WPA_KEY_ID_LEN];			/* WPA:Key ID, 802.11i/WPA2: Reserved */
	u8 mic[WPA_KEY_MIC_LEN];		/* Key MIC */
	u16 data_len;				/* Key Data Length */
	u8 data[0];				/* Key data */
} __attribute__((packed));

/* DHCP options */
struct dhcp_option {
	u8 code;
	u8 length;
	u8 value[0];
};

enum ieee802_1x_eapol_type {
	IEEE802_1X_TYPE_EAP_PACKET = 0,
	IEEE802_1X_TYPE_EAPOL_START,
	IEEE802_1X_TYPE_EAPOL_LOGOFF,
	IEEE802_1X_TYPE_EAPOL_KEY,
	IEEE802_1X_TYPE_EAPOL_ENCAPSULATED_ASF_ALERT,
	IEEE802_1X_TYPE_EAPOL_MKA,
};

/* WPS message type */
enum wps_msg_type {
	WPS_Beacon = 1,
	WPS_ProbeRequest,
	WPS_ProbeResponse,
	WPS_M1,
	WPS_M2,
	WPS_M2D,
	WPS_M3,
	WPS_M4,
	WPS_M5,
	WPS_M6,
	WPS_M7,
	WPS_M8,
	WPS_WSC_ACK,
	WPS_WSC_NACK,
	WPS_WSC_DONE
};

/* EAPOL code */
enum eapol_code_status {
	EAP_CODE_REQUEST = 1,
	EAP_CODE_RESPONSE,
	EAP_CODE_SUCCESS,
	EAP_CODE_FAILURE,
	EAP_CODE_INITIATE,
	EAP_CODE_FINISH,
};

/* EAPOL type */
enum eapol_type {
	EAP_TYPE_NONE = 0,
	EAP_TYPE_IDENTITY = 1 /* RFC 3748 */,
	EAP_TYPE_NOTIFICATION = 2 /* RFC 3748 */,
	EAP_TYPE_NAK = 3 /* Response only, RFC 3748 */,
	EAP_TYPE_MD5 = 4, /* RFC 3748 */
	EAP_TYPE_OTP = 5 /* RFC 3748 */,
	EAP_TYPE_GTC = 6, /* RFC 3748 */
	EAP_TYPE_TLS = 13 /* RFC 2716 */,
	EAP_TYPE_LEAP = 17 /* Cisco proprietary */,
	EAP_TYPE_SIM = 18 /* RFC 4186 */,
	EAP_TYPE_TTLS = 21 /* RFC 5281 */,
	EAP_TYPE_AKA = 23 /* RFC 4187 */,
	EAP_TYPE_PEAP = 25 /* draft-josefsson-pppext-eap-tls-eap-06.txt */,
	EAP_TYPE_MSCHAPV2 = 26 /* draft-kamath-pppext-eap-mschapv2-00.txt */,
	EAP_TYPE_TLV = 33 /* draft-josefsson-pppext-eap-tls-eap-07.txt */,
	EAP_TYPE_TNC = 38 /* TNC IF-T v1.0-r3; note: tentative assignment;
			   * type 38 has previously been allocated for
			   * EAP-HTTP Digest, (funk.com) */,
	EAP_TYPE_FAST = 43 /* RFC 4851 */,
	EAP_TYPE_PAX = 46 /* RFC 4746 */,
	EAP_TYPE_PSK = 47 /* RFC 4764 */,
	EAP_TYPE_SAKE = 48 /* RFC 4763 */,
	EAP_TYPE_IKEV2 = 49 /* RFC 5106 */,
	EAP_TYPE_AKA_PRIME = 50 /* RFC 5448 */,
	EAP_TYPE_GPSK = 51 /* RFC 5433 */,
	EAP_TYPE_PWD = 52 /* RFC 5931 */,
	EAP_TYPE_EKE = 53 /* RFC 6124 */,
	EAP_TYPE_TEAP = 55 /* RFC 7170 */,
	EAP_TYPE_EXPANDED = 254 /* RFC 3748 */
};

/* EAP-WSC OP-Code values */
enum wsc_op_code {
	WSC_UPNP = 0 /* No OP Code in UPNP transport */,
	WSC_START,
	WSC_ACK,
	WSC_NACK,
	WSC_MSG,
	WSC_DONE,
	WSC_FRAG_ACK,
};

/* P2P public action frame type */
enum oui_subtype {
	P2P_GO_NEGO_REQ = 0,
	P2P_GO_NEGO_RESP,
	P2P_GO_NEGO_CONF,
	P2P_INVIT_REQ,
	P2P_INVIT_RESP,
	P2P_DEVDISC_REQ,
	P2P_DEVDISC_RESP,
	P2P_PROVISION_DISC_REQ,
	P2P_PROVISION_DISC_RESP,
};

/* Frame transmission state */
enum frame_tx_rx_status {
	FRAME_STATUS_TX = 0,
	FRAME_STATUS_TX_CFM,
	FRAME_STATUS_TX_WITHOUT_TXHDR,
	FRAME_STATUS_RX,
};

struct bes_sk_param {
	void *param_ptr;
	int offset;
	int size;
};

struct exte_auth_protocol {
	u8 code;
	u8 identifier;
	u16 length;
	u8 type;
};

#ifndef ETH_P_WAPI
#define ETH_P_WAPI     0x88B4
#endif

#define WPI_HDR_LEN 18
#define WPI_PN_LEN 16
#define WPI_PN_OFST 2
#define WPI_MIC_LEN 16
#define WPI_KEY_LEN 32
#define WPI_SUBKEY_LEN 16 // WPI key is actually two 16bytes key

static inline u16 WPA_GET_BE16(const u8 *a)
{
	return (a[0] << 8) | a[1];
}

static inline void WPA_PUT_BE16(u8 *a, u16 val)
{
	a[0] = val >> 8;
	a[1] = val & 0xff;
}

static inline u16 WPA_GET_LE16(const u8 *a)
{
	return (a[1] << 8) | a[0];
}

static inline void WPA_PUT_LE16(u8 *a, u16 val)
{
	a[1] = val >> 8;
	a[0] = val & 0xff;
}

static inline u32 WPA_GET_BE24(const u8 *a)
{
	return (a[0] << 16) | (a[1] << 8) | a[2];
}

static inline void WPA_PUT_BE24(u8 *a, u32 val)
{
	a[0] = (val >> 16) & 0xff;
	a[1] = (val >> 8) & 0xff;
	a[2] = val & 0xff;
}

static inline u32 WPA_GET_BE32(const u8 *a)
{
	return ((u32) a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

static inline void WPA_PUT_BE32(u8 *a, u32 val)
{
	a[0] = (val >> 24) & 0xff;
	a[1] = (val >> 16) & 0xff;
	a[2] = (val >> 8) & 0xff;
	a[3] = val & 0xff;
}

static inline u32 WPA_GET_LE32(const u8 *a)
{
	return ((u32) a[3] << 24) | (a[2] << 16) | (a[1] << 8) | a[0];
}

static inline void WPA_PUT_LE32(u8 *a, u32 val)
{
	a[3] = (val >> 24) & 0xff;
	a[2] = (val >> 16) & 0xff;
	a[1] = (val >> 8) & 0xff;
	a[0] = val & 0xff;
}

static inline u64 WPA_GET_BE64(const u8 *a)
{
	return (((u64) a[0]) << 56) | (((u64) a[1]) << 48) |
		(((u64) a[2]) << 40) | (((u64) a[3]) << 32) |
		(((u64) a[4]) << 24) | (((u64) a[5]) << 16) |
		(((u64) a[6]) << 8) | ((u64) a[7]);
}

static inline void WPA_PUT_BE64(u8 *a, u64 val)
{
	a[0] = val >> 56;
	a[1] = val >> 48;
	a[2] = val >> 40;
	a[3] = val >> 32;
	a[4] = val >> 24;
	a[5] = val >> 16;
	a[6] = val >> 8;
	a[7] = val & 0xff;
}

static inline u64 WPA_GET_LE64(const u8 *a)
{
	return (((u64) a[7]) << 56) | (((u64) a[6]) << 48) |
		(((u64) a[5]) << 40) | (((u64) a[4]) << 32) |
		(((u64) a[3]) << 24) | (((u64) a[2]) << 16) |
		(((u64) a[1]) << 8) | ((u64) a[0]);
}

static inline void WPA_PUT_LE64(u8 *a, u64 val)
{
	a[7] = val >> 56;
	a[6] = val >> 48;
	a[5] = val >> 40;
	a[4] = val >> 32;
	a[3] = val >> 24;
	a[2] = val >> 16;
	a[1] = val >> 8;
	a[0] = val & 0xff;
}

static inline bool is_ipv4_multicast_ether_addr(const u8 *addr)
{
	const u8 ipv4_multicast[3] = {
		0x01, 0x00, 0x5e
	};

	if (memcmp(addr, ipv4_multicast, 3) == 0)
		return true;

	return false;
}

static inline bool is_ipv6_multicast_ether_addr(const u8 *addr)
{
	const u8 ipv6_multicast[2] = {
		0x33, 0x33
	};

	if (memcmp(addr, ipv6_multicast, 2) == 0)
		return true;

	return false;
}

int bes_common_parse_line(char *line, char *argv[], int maxargs);
u32 bes_common_get_vif_idx_map(struct rwnx_hw *rwnx_hw);
int bes_common_vif_idx(struct rwnx_hw *rwnx_hw, char *name);
bool bes_common_parse_dhcp_ack_frame(struct sk_buff *skb, u32 *gateway_ip);
int wpa_parse_wpa_ie_wpa(const u8 *wpa_ie, size_t wpa_ie_len, struct wpa_ie_data *data);
int wpa_parse_wpa_ie_rsn(const u8 *rsn_ie, size_t rsn_ie_len, struct wpa_ie_data *data);
void bes_encryp_analyze(struct cfg80211_bss *bss, struct ieee80211_mgmt *mgmt, u8 *security);
void *bes_common_get_param_ptr(struct sk_buff *skb, struct bes_sk_param *sk_param_ptr, u8 tx_rx_status);
#endif /* _BES_COMMON_ */
