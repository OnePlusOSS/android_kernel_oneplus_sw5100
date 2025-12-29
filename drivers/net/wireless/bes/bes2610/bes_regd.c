/*
 * @file bes_reg.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/rtnetlink.h>

#include "rwnx_defs.h"
#include "hal_desc.h"
#include "rwnx_cfgfile.h"
#include "reg_access.h"
#include "bes_log.h"
#include "bes_regd.h"
#include "rwnx_msg_tx.h"
#include "rwnx_main.h"

/* country 00: This is the world regulatory domain */
static struct ieee80211_regdomain bes_regdom_00 = {
	.n_reg_rules = 8,
	.alpha2 = "99",
	.reg_rules = {
		REG_RULE(755, 928, 2, 0, 20, NL80211_RRF_NO_IR),
		REG_RULE(2402, 2472, 40, 0, 20, 0),
		REG_RULE(2457, 2482, 20, 0, 20, NL80211_RRF_NO_IR|NL80211_RRF_AUTO_BW),
		REG_RULE(2474, 2494, 20, 0, 20, NL80211_RRF_NO_IR|NL80211_RRF_NO_OFDM),
		REG_RULE(5170, 5250, 80, 0, 20, NL80211_RRF_NO_IR|NL80211_RRF_AUTO_BW),
		REG_RULE(5250, 5330, 80, 0, 20, NL80211_RRF_NO_IR|NL80211_RRF_DFS|NL80211_RRF_AUTO_BW),
		REG_RULE(5490, 5730, 160, 0, 20, NL80211_RRF_NO_IR|NL80211_RRF_DFS),
		REG_RULE(5735, 5835, 80, 0, 20, NL80211_RRF_NO_IR),
	}
};

/* country AE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ae = {
	.n_reg_rules = 5,
	.alpha2 = "AE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country AT: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_at = {
	.n_reg_rules = 5,
	.alpha2 = "AT",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country BH: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_bh = {
	.n_reg_rules = 4,
	.alpha2 = "BH",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_149_165(33),
	}
};

/* country BO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_bo = {
	.n_reg_rules = 5,
	.alpha2 = "BO",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country BY: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_by = {
	.n_reg_rules = 4,
	.alpha2 = "BY",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
	}
};

/* country CL: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_cl = {
	.n_reg_rules = 5,
	.alpha2 = "CL",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(30),
		reg_rule_5g_52_64_dfs(30),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(30),
	}
};

/* country CO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_co = {
	.n_reg_rules = 5,
	.alpha2 = "CO",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country CR: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_cr = {
	.n_reg_rules = 5,
	.alpha2 = "CR",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country CZ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_cz = {
	.n_reg_rules = 5,
	.alpha2 = "CZ",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country PK: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_pk = {
	.n_reg_rules = 3,
	.alpha2 = "PK",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_149_161(30),
		reg_rule_5g_165_dfs(30),
	}
};

/* country DE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_de = {
	.n_reg_rules = 5,
	.alpha2 = "DE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country EE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ee = {
	.n_reg_rules = 5,
	.alpha2 = "EE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country EG: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_eg = {
	.n_reg_rules = 3,
	.alpha2 = "EG",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
	}
};

/* country ES: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_es = {
	.n_reg_rules = 5,
	.alpha2 = "ES",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country FI: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_fi = {
	.n_reg_rules = 5,
	.alpha2 = "FI",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country AZ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_az = {
	.n_reg_rules = 5,
	.alpha2 = "AZ",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country KG: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_kg = {
	.n_reg_rules = 5,
	.alpha2 = "KG",
	.reg_rules = {
		reg_rule_2g_1_12(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country KZ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_kz = {
	.n_reg_rules = 5,
	.alpha2 = "KZ",
	.reg_rules = {
		reg_rule_2g_1_13(23),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(23),
		reg_rule_5g_149_165(23),
	}
};

/* country GE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ge = {
	.n_reg_rules = 4,
	.alpha2 = "GE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
	}
};

/* country TM: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_tm = {
	.n_reg_rules = 5,
	.alpha2 = "TM",
	.reg_rules = {
		reg_rule_2g_1_12(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country UZ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_uz = {
	.n_reg_rules = 5,
	.alpha2 = "UZ",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country MN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mn = {
	.n_reg_rules = 5,
	.alpha2 = "MN",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country AM: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_am = {
	.n_reg_rules = 3,
	.alpha2 = "AM",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
	}
};

/* country FR: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_fr = {
	.n_reg_rules = 5,
	.alpha2 = "FR",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country GB: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_gb = {
	.n_reg_rules = 5,
	.alpha2 = "GB",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country GR: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_gr = {
	.n_reg_rules = 5,
	.alpha2 = "GR",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country GT: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_gt = {
	.n_reg_rules = 5,
	.alpha2 = "GT",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(27),
	}
};

/* country HR: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_hr = {
	.n_reg_rules = 5,
	.alpha2 = "HR",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country EC: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ec = {
	.n_reg_rules = 5,
	.alpha2 = "EC",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country HU: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_hu = {
	.n_reg_rules = 5,
	.alpha2 = "HU",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country PA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_pa = {
	.n_reg_rules = 5,
	.alpha2 = "PA",
	.reg_rules = {
		reg_rule_2g_1_11(36),
		reg_rule_5g_36_48(30),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country HN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_hn = {
	.n_reg_rules = 5,
	.alpha2 = "HN",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country SV: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_sv = {
	.n_reg_rules = 4,
	.alpha2 = "SV",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country IQ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_iq = {
	.n_reg_rules = 4,
	.alpha2 = "IQ",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
	}
};

/* country PY: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_py = {
	.n_reg_rules = 5,
	.alpha2 = "PY",
	.reg_rules = {
		reg_rule_2g_1_11(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country UY: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_uy = {
	.n_reg_rules = 7,
	.alpha2 = "UY",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_116_dfs(23),
		reg_rule_5g_132_136_dfs(23),
		reg_rule_5g_140_dfs(23),
		reg_rule_5g_149_165(30),
	}
};

/* country IT: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_it = {
	.n_reg_rules = 5,
	.alpha2 = "IT",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country TH: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_th = {
	.n_reg_rules = 5,
	.alpha2 = "TH",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(33),
	}
};

/* country PH: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ph = {
	.n_reg_rules = 5,
	.alpha2 = "PH",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(20),
	}
};

/* country MM: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mm = {
	.n_reg_rules = 5,
	.alpha2 = "MM",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country LA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_la = {
	.n_reg_rules = 5,
	.alpha2 = "LA",
	.reg_rules = {
		reg_rule_2g_1_12(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country NP: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_np = {
	.n_reg_rules = 5,
	.alpha2 = "NP",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(20),
		reg_rule_5g_52_64_dfs(20),
		reg_rule_5g_100_128_dfs(20),
		reg_rule_5g_149_165(20),
	}
};

/* country KH: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_kh = {
	.n_reg_rules = 5,
	.alpha2 = "KH",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(20),
	}
};

/* country JO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_jo = {
	.n_reg_rules = 4,
	.alpha2 = "JO",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(23),
	}
};

/* country KW: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_kw = {
	.n_reg_rules = 5,
	.alpha2 = "KW",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(30),
	}
};

/* country LT: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_lt = {
	.n_reg_rules = 5,
	.alpha2 = "LT",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country LV: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_lv = {
	.n_reg_rules = 5,
	.alpha2 = "LV",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country MA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ma = {
	.n_reg_rules = 3,
	.alpha2 = "MA",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
	}
};

/* country MX: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mx = {
	.n_reg_rules = 7,
	.alpha2 = "MX",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48_dfs(17),
		reg_rule_5g_52_64(24),
		reg_rule_5g_100_116_dfs(24),
		reg_rule_5g_132_136_dfs(24),
		reg_rule_5g_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country MY: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_my = {
	.n_reg_rules = 5,
	.alpha2 = "MY",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(30),
		reg_rule_5g_52_64_dfs(30),
		reg_rule_5g_100_128_dfs(30),
		reg_rule_5g_149_165(30),
	}
};

/* country OM: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_om = {
	.n_reg_rules = 4,
	.alpha2 = "OM",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
	}
};

/* country PE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_pe = {
	.n_reg_rules = 5,
	.alpha2 = "PE",
	.reg_rules = {
		reg_rule_2g_1_11(20),
		reg_rule_5g_36_48_dfs(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country PL: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_pl = {
	.n_reg_rules = 5,
	.alpha2 = "PL",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country QA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_qa = {
	.n_reg_rules = 5,
	.alpha2 = "QA",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(20),
		reg_rule_5g_149_165(20),
	}
};

/* country RO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ro = {
	.n_reg_rules = 5,
	.alpha2 = "RO",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country RS: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_rs = {
	.n_reg_rules = 5,
	.alpha2 = "RS",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(14),
	}
};

/* country RU: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ru = {
	.n_reg_rules = 6,
	.alpha2 = "RU",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_132_136_dfs(30),
		reg_rule_5g_140_dfs(30),
		reg_rule_5g_149_165(30),
	}
};

/* country SA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_sa = {
	.n_reg_rules = 5,
	.alpha2 = "SA",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165_dfs(14),
	}
};

/* country SI: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_si = {
	.n_reg_rules = 5,
	.alpha2 = "SI",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country SK: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_sk = {
	.n_reg_rules = 5,
	.alpha2 = "SK",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country TR: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_tr = {
	.n_reg_rules = 5,
	.alpha2 = "TR",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165_dfs(14),
	}
};

/* country BG: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_bg = {
	.n_reg_rules = 5,
	.alpha2 = "BG",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country UA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ua = {
	.n_reg_rules = 5,
	.alpha2 = "UA",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(20),
		reg_rule_5g_52_64_dfs(20),
		reg_rule_5g_100_140_dfs(20),
		reg_rule_5g_149_165(20),
	}
};

/* country BA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ba = {
	.n_reg_rules = 5,
	.alpha2 = "BA",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country ME: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_me = {
	.n_reg_rules = 5,
	.alpha2 = "ME",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country MK: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mk = {
	.n_reg_rules = 5,
	.alpha2 = "MK",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(14),
	}
};

/* country AL: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_al = {
	.n_reg_rules = 5,
	.alpha2 = "AL",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country CY: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_cy = {
	.n_reg_rules = 5,
	.alpha2 = "CY",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country MD: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_md = {
	.n_reg_rules = 5,
	.alpha2 = "MD",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country ZA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_za = {
	.n_reg_rules = 5,
	.alpha2 = "ZA",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country DK: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_dk = {
	.n_reg_rules = 5,
	.alpha2 = "DK",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country NO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_no = {
	.n_reg_rules = 5,
	.alpha2 = "NO",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(20),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165_dfs(14),
	}
};

/* country SE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_se = {
	.n_reg_rules = 5,
	.alpha2 = "SE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country TJ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_tj = {
	.n_reg_rules = 5,
	.alpha2 = "TJ",
	.reg_rules = {
		reg_rule_2g_1_12(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(14),
	}
};

/* country CN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_cn = {
	.n_reg_rules = 4,
	.alpha2 = "CN",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_149_165(33),
	}
};

/* country BE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_be = {
	.n_reg_rules = 5,
	.alpha2 = "BE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country LU: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_lu = {
	.n_reg_rules = 5,
	.alpha2 = "LU",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country NL: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_nl = {
	.n_reg_rules = 5,
	.alpha2 = "NL",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country PT: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_pt = {
	.n_reg_rules = 5,
	.alpha2 = "PT",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country IN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_in = {
	.n_reg_rules = 5,
	.alpha2 = "IN",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_36_48(30),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country HK: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_hk = {
	.n_reg_rules = 5,
	.alpha2 = "HK",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(33),
	}
};

/* country LB: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_lb = {
	.n_reg_rules = 5,
	.alpha2 = "LB",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country DO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_do = {
	.n_reg_rules = 5,
	.alpha2 = "DO",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};


/* country LK: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_lk = {
	.n_reg_rules = 5,
	.alpha2 = "LK",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country VN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_vn = {
	.n_reg_rules = 5,
	.alpha2 = "VN",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country PG: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_pg = {
	.n_reg_rules = 5,
	.alpha2 = "PG",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165_dfs(30),
	}
};

/* country SG: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_sg = {
	.n_reg_rules = 5,
	.alpha2 = "SG",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(33),
	}
};

/* country NZ: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_nz = {
	.n_reg_rules = 5,
	.alpha2 = "NZ",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(36),
	}
};

/* country AU: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_au = {
	.n_reg_rules = 7,
	.alpha2 = "AU",
	.reg_rules = {
		reg_rule_2g_1_13(30),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_116_dfs(30),
		reg_rule_5g_132_136_dfs(30),
		reg_rule_5g_140_dfs(30),
		reg_rule_5g_149_165(36),
	}
};

/* country TW: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_tw = {
	.n_reg_rules = 5,
	.alpha2 = "TW",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country CA: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ca = {
	.n_reg_rules = 7,
	.alpha2 = "CA",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_116_dfs(24),
		reg_rule_5g_132_136_dfs(24),
		reg_rule_5g_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country NI: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ni = {
	.n_reg_rules = 5,
	.alpha2 = "NI",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(17),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country BN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_bn = {
	.n_reg_rules = 5,
	.alpha2 = "BN",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country CH: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ch = {
	.n_reg_rules = 5,
	.alpha2 = "CH",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country MT: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mt = {
	.n_reg_rules = 5,
	.alpha2 = "MT",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country IE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ie = {
	.n_reg_rules = 5,
	.alpha2 = "IE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country IL: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_il = {
	.n_reg_rules = 5,
	.alpha2 = "IL",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country MO: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mo = {
	.n_reg_rules = 5,
	.alpha2 = "MO",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_144_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country MU: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_mu = {
	.n_reg_rules = 5,
	.alpha2 = "MU",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(30),
		reg_rule_5g_149_165(14),
	}
};

/* country TN: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_tn = {
	.n_reg_rules = 4,
	.alpha2 = "TN",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_116_dfs(30),
	}
};

/* country YE: DFS-ETSI */
static struct ieee80211_regdomain bes_regdom_ye = {
	.n_reg_rules = 4,
	.alpha2 = "YE",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48_dfs(23),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_140_dfs(30),
	}
};

/* country BR: DFS-FCC */
static struct ieee80211_regdomain bes_regdom_br = {
	.n_reg_rules = 5,
	.alpha2 = "BR",
	.reg_rules = {
		reg_rule_2g_1_11(20),
		reg_rule_5g_36_48(27),
		reg_rule_5g_52_64_dfs(27),
		reg_rule_5g_100_140_dfs(27),
		reg_rule_5g_149_165(30),
	}
};

/* country US: DFS-FCC */
static struct ieee80211_regdomain bes_regdom_us = {
	.n_reg_rules = 5,
	.alpha2 = "US",
	.reg_rules = {
		reg_rule_2g_1_11(30),
		reg_rule_5g_36_48(24),
		reg_rule_5g_52_64_dfs(24),
		reg_rule_5g_100_140_dfs(24),
		reg_rule_5g_149_165(30),
	}
};

/* country BD: DFS-JP */
static struct ieee80211_regdomain bes_regdom_bd = {
	.n_reg_rules = 5,
	.alpha2 = "BD",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(30),
		reg_rule_5g_52_64_dfs(30),
		reg_rule_5g_100_140_dfs(30),
		reg_rule_5g_149_165(30),
	}
};

/* country JP: DFS-JP */
static struct ieee80211_regdomain bes_regdom_jp = {
	.n_reg_rules = 4,
	.alpha2 = "JP",
	.reg_rules = {
		reg_rule_2g_1_13(20),
		reg_rule_5g_36_48(20),
		reg_rule_5g_52_64_dfs(23),
		reg_rule_5g_100_144_dfs(23),
	}
};

/* country ID: DFS-JP */
static struct ieee80211_regdomain bes_regdom_id= {
	.n_reg_rules = 4,
	.alpha2 = "ID",
	.reg_rules = {
		reg_rule_2g_1_13(27),
		reg_rule_5g_36_48(23),
		reg_rule_5g_52_64(23),
		reg_rule_5g_149_161(30),
	}
};

/* Regulatory rules */
static struct bes_regdom_param bes_regdom[] = {
	{.country_code_id = COUNTRY_CODE_AE, .regdom_param = &bes_regdom_ae},
	{.country_code_id = COUNTRY_CODE_AT, .regdom_param = &bes_regdom_at},
	{.country_code_id = COUNTRY_CODE_BH, .regdom_param = &bes_regdom_bh},
	{.country_code_id = COUNTRY_CODE_BO, .regdom_param = &bes_regdom_bo},
	{.country_code_id = COUNTRY_CODE_BY, .regdom_param = &bes_regdom_by},
	{.country_code_id = COUNTRY_CODE_CL, .regdom_param = &bes_regdom_cl},
	{.country_code_id = COUNTRY_CODE_CO, .regdom_param = &bes_regdom_co},
	{.country_code_id = COUNTRY_CODE_CR, .regdom_param = &bes_regdom_cr},
	{.country_code_id = COUNTRY_CODE_CZ, .regdom_param = &bes_regdom_cz},
	{.country_code_id = COUNTRY_CODE_PK, .regdom_param = &bes_regdom_pk},
	{.country_code_id = COUNTRY_CODE_DE, .regdom_param = &bes_regdom_de},
	{.country_code_id = COUNTRY_CODE_EE, .regdom_param = &bes_regdom_ee},
	{.country_code_id = COUNTRY_CODE_EG, .regdom_param = &bes_regdom_eg},
	{.country_code_id = COUNTRY_CODE_ES, .regdom_param = &bes_regdom_es},
	{.country_code_id = COUNTRY_CODE_FI, .regdom_param = &bes_regdom_fi},
	{.country_code_id = COUNTRY_CODE_AZ, .regdom_param = &bes_regdom_az},
	{.country_code_id = COUNTRY_CODE_KG, .regdom_param = &bes_regdom_kg},
	{.country_code_id = COUNTRY_CODE_KZ, .regdom_param = &bes_regdom_kz},
	{.country_code_id = COUNTRY_CODE_GE, .regdom_param = &bes_regdom_ge},
	{.country_code_id = COUNTRY_CODE_TM, .regdom_param = &bes_regdom_tm},
	{.country_code_id = COUNTRY_CODE_UZ, .regdom_param = &bes_regdom_uz},
	{.country_code_id = COUNTRY_CODE_MN, .regdom_param = &bes_regdom_mn},
	{.country_code_id = COUNTRY_CODE_AM, .regdom_param = &bes_regdom_am},
	{.country_code_id = COUNTRY_CODE_FR, .regdom_param = &bes_regdom_fr},
	{.country_code_id = COUNTRY_CODE_GB, .regdom_param = &bes_regdom_gb},
	{.country_code_id = COUNTRY_CODE_GR, .regdom_param = &bes_regdom_gr},
	{.country_code_id = COUNTRY_CODE_GT, .regdom_param = &bes_regdom_gt},
	{.country_code_id = COUNTRY_CODE_HR, .regdom_param = &bes_regdom_hr},
	{.country_code_id = COUNTRY_CODE_EC, .regdom_param = &bes_regdom_ec},
	{.country_code_id = COUNTRY_CODE_HU, .regdom_param = &bes_regdom_hu},
	{.country_code_id = COUNTRY_CODE_PA, .regdom_param = &bes_regdom_pa},
	{.country_code_id = COUNTRY_CODE_HN, .regdom_param = &bes_regdom_hn},
	{.country_code_id = COUNTRY_CODE_SV, .regdom_param = &bes_regdom_sv},
	{.country_code_id = COUNTRY_CODE_IQ, .regdom_param = &bes_regdom_iq},
	{.country_code_id = COUNTRY_CODE_PY, .regdom_param = &bes_regdom_py},
	{.country_code_id = COUNTRY_CODE_UY, .regdom_param = &bes_regdom_uy},
	{.country_code_id = COUNTRY_CODE_IT, .regdom_param = &bes_regdom_it},
	{.country_code_id = COUNTRY_CODE_TH, .regdom_param = &bes_regdom_th},
	{.country_code_id = COUNTRY_CODE_PH, .regdom_param = &bes_regdom_ph},
	{.country_code_id = COUNTRY_CODE_MM, .regdom_param = &bes_regdom_mm},
	{.country_code_id = COUNTRY_CODE_LA, .regdom_param = &bes_regdom_la},
	{.country_code_id = COUNTRY_CODE_NP, .regdom_param = &bes_regdom_np},
	{.country_code_id = COUNTRY_CODE_KH, .regdom_param = &bes_regdom_kh},
	{.country_code_id = COUNTRY_CODE_JO, .regdom_param = &bes_regdom_jo},
	{.country_code_id = COUNTRY_CODE_KW, .regdom_param = &bes_regdom_kw},
	{.country_code_id = COUNTRY_CODE_LT, .regdom_param = &bes_regdom_lt},
	{.country_code_id = COUNTRY_CODE_LV, .regdom_param = &bes_regdom_lv},
	{.country_code_id = COUNTRY_CODE_MA, .regdom_param = &bes_regdom_ma},
	{.country_code_id = COUNTRY_CODE_MX, .regdom_param = &bes_regdom_mx},
	{.country_code_id = COUNTRY_CODE_MY, .regdom_param = &bes_regdom_my},
	{.country_code_id = COUNTRY_CODE_OM, .regdom_param = &bes_regdom_om},
	{.country_code_id = COUNTRY_CODE_PE, .regdom_param = &bes_regdom_pe},
	{.country_code_id = COUNTRY_CODE_PL, .regdom_param = &bes_regdom_pl},
	{.country_code_id = COUNTRY_CODE_QA, .regdom_param = &bes_regdom_qa},
	{.country_code_id = COUNTRY_CODE_RO, .regdom_param = &bes_regdom_ro},
	{.country_code_id = COUNTRY_CODE_RS, .regdom_param = &bes_regdom_rs},
	{.country_code_id = COUNTRY_CODE_RU, .regdom_param = &bes_regdom_ru},
	{.country_code_id = COUNTRY_CODE_SA, .regdom_param = &bes_regdom_sa},
	{.country_code_id = COUNTRY_CODE_SI, .regdom_param = &bes_regdom_si},
	{.country_code_id = COUNTRY_CODE_SK, .regdom_param = &bes_regdom_sk},
	{.country_code_id = COUNTRY_CODE_TR, .regdom_param = &bes_regdom_tr},
	{.country_code_id = COUNTRY_CODE_BG, .regdom_param = &bes_regdom_bg},
	{.country_code_id = COUNTRY_CODE_UA, .regdom_param = &bes_regdom_ua},
	{.country_code_id = COUNTRY_CODE_BA, .regdom_param = &bes_regdom_ba},
	{.country_code_id = COUNTRY_CODE_ME, .regdom_param = &bes_regdom_me},
	{.country_code_id = COUNTRY_CODE_MK, .regdom_param = &bes_regdom_mk},
	{.country_code_id = COUNTRY_CODE_AL, .regdom_param = &bes_regdom_al},
	{.country_code_id = COUNTRY_CODE_CY, .regdom_param = &bes_regdom_cy},
	{.country_code_id = COUNTRY_CODE_MD, .regdom_param = &bes_regdom_md},
	{.country_code_id = COUNTRY_CODE_ZA, .regdom_param = &bes_regdom_za},
	{.country_code_id = COUNTRY_CODE_DK, .regdom_param = &bes_regdom_dk},
	{.country_code_id = COUNTRY_CODE_NO, .regdom_param = &bes_regdom_no},
	{.country_code_id = COUNTRY_CODE_SE, .regdom_param = &bes_regdom_se},
	{.country_code_id = COUNTRY_CODE_TJ, .regdom_param = &bes_regdom_tj},
	{.country_code_id = COUNTRY_CODE_CN, .regdom_param = &bes_regdom_cn},
	{.country_code_id = COUNTRY_CODE_BE, .regdom_param = &bes_regdom_be},
	{.country_code_id = COUNTRY_CODE_LU, .regdom_param = &bes_regdom_lu},
	{.country_code_id = COUNTRY_CODE_NL, .regdom_param = &bes_regdom_nl},
	{.country_code_id = COUNTRY_CODE_PT, .regdom_param = &bes_regdom_pt},
	{.country_code_id = COUNTRY_CODE_IN, .regdom_param = &bes_regdom_in},
	{.country_code_id = COUNTRY_CODE_HK, .regdom_param = &bes_regdom_hk},
	{.country_code_id = COUNTRY_CODE_LB, .regdom_param = &bes_regdom_lb},
	{.country_code_id = COUNTRY_CODE_DO, .regdom_param = &bes_regdom_do},
	{.country_code_id = COUNTRY_CODE_LK, .regdom_param = &bes_regdom_lk},
	{.country_code_id = COUNTRY_CODE_VN, .regdom_param = &bes_regdom_vn},
	{.country_code_id = COUNTRY_CODE_PG, .regdom_param = &bes_regdom_pg},
	{.country_code_id = COUNTRY_CODE_SG, .regdom_param = &bes_regdom_sg},
	{.country_code_id = COUNTRY_CODE_NZ, .regdom_param = &bes_regdom_nz},
	{.country_code_id = COUNTRY_CODE_AU, .regdom_param = &bes_regdom_au},
	{.country_code_id = COUNTRY_CODE_TW, .regdom_param = &bes_regdom_tw},
	{.country_code_id = COUNTRY_CODE_CA, .regdom_param = &bes_regdom_ca},
	{.country_code_id = COUNTRY_CODE_NI, .regdom_param = &bes_regdom_ni},
	{.country_code_id = COUNTRY_CODE_BN, .regdom_param = &bes_regdom_bn},
	{.country_code_id = COUNTRY_CODE_CH, .regdom_param = &bes_regdom_ch},
	{.country_code_id = COUNTRY_CODE_MT, .regdom_param = &bes_regdom_mt},
	{.country_code_id = COUNTRY_CODE_IE, .regdom_param = &bes_regdom_ie},
	{.country_code_id = COUNTRY_CODE_IL, .regdom_param = &bes_regdom_il},
	{.country_code_id = COUNTRY_CODE_MO, .regdom_param = &bes_regdom_mo},
	{.country_code_id = COUNTRY_CODE_MU, .regdom_param = &bes_regdom_mu},
	{.country_code_id = COUNTRY_CODE_TN, .regdom_param = &bes_regdom_tn},
	{.country_code_id = COUNTRY_CODE_YE, .regdom_param = &bes_regdom_ye},
	{.country_code_id = COUNTRY_CODE_BR, .regdom_param = &bes_regdom_br},
	{.country_code_id = COUNTRY_CODE_US, .regdom_param = &bes_regdom_us},
	{.country_code_id = COUNTRY_CODE_BD, .regdom_param = &bes_regdom_bd},
	{.country_code_id = COUNTRY_CODE_JP, .regdom_param = &bes_regdom_jp},
	{.country_code_id = COUNTRY_CODE_ID, .regdom_param = &bes_regdom_id},
};

struct ieee80211_regdomain *bes_regd_get_regdom(char *country_code)
{
	int regdom_idx;
	struct ieee80211_regdomain * regdomain = NULL;
	u16 country_num = (((u16)country_code[0] << 8) | (u16)country_code[1]);
	int regdom_sum = (sizeof(bes_regdom) / sizeof(struct bes_regdom_param));

	if (memcmp(country_code, "00", 2) == 0) {
		return &bes_regdom_00;
	}

	for (regdom_idx = 0; regdom_idx < regdom_sum; regdom_idx++) {
		if (country_num == bes_regdom[regdom_idx].country_code_id) {
			regdomain = bes_regdom[regdom_idx].regdom_param;
			break;
		}
	}

	if (regdomain == NULL) {
		memcpy(country_code, "00", 2);
		regdomain = &bes_regdom_00;
	}

	return regdomain;
}

void bes_regd_no_ir_update_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw, custregd.no_ir_update_work);

	if (rwnx_wlan_enabled(rwnx_hw))
		rwnx_send_me_chan_config_req(rwnx_hw);
}

void bes_regd_no_ir_update(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->custregd.no_ir_update_num != 0) {
		rwnx_hw->custregd.no_ir_update_num = 0;
		queue_work(rwnx_hw->rwnx_wq, &(rwnx_hw->custregd.no_ir_update_work));
	}
}

static bool bes_regd_freq_is_chan_12_13_14(u16 freq)
{
	if (freq == ieee80211_channel_to_frequency(12, NL80211_BAND_2GHZ) ||
	    freq == ieee80211_channel_to_frequency(13, NL80211_BAND_2GHZ) ||
	    freq == ieee80211_channel_to_frequency(14, NL80211_BAND_2GHZ))
		return true;
	return false;
}

void bes_regd_chan_no_ir_update(struct rwnx_hw *rwnx_hw, struct ieee80211_channel *chan)
{
	if (chan->beacon_found || chan->flags & IEEE80211_CHAN_RADAR ||
	    (chan->band == NL80211_BAND_2GHZ &&
	    !bes_regd_freq_is_chan_12_13_14(chan->center_freq))) {
		if (chan->flags & IEEE80211_CHAN_NO_IR) {
			chan->flags &= ~IEEE80211_CHAN_NO_IR;
			rwnx_hw->custregd.no_ir_update_num++;
		}
	}
}

int bes_regd_update_custregd(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy, char *country_code)
{
	char ori_country_code[2] = {0};
	struct ieee80211_regdomain *bes_regdom;
	int ret;

	memcpy(ori_country_code, country_code, 2);
	rwnx_hw->custregd.no_ir_update_num = 0;
	bes_regdom = bes_regd_get_regdom(country_code);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	if (rtnl_is_locked()) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
		wiphy_lock(wiphy);
		ret = regulatory_set_wiphy_regd_sync(wiphy, bes_regdom);
		wiphy_unlock(wiphy);
#else
		ret = regulatory_set_wiphy_regd_sync_rtnl(wiphy, bes_regdom);
#endif /* LINUX_VERSION >= 5.12.0 */
	} else {
		ret = regulatory_set_wiphy_regd(wiphy, bes_regdom);
	}
#else
	ret = wiphy_apply_custom_regulatory(wiphy, bes_regdom);
#endif /* LINUX_VERSION >= 4.0.0 */

	if (ret) {
		wiphy_err(wiphy, "set regdomain faied %d.\n", ret);
		return ret;
	}
	wiphy_info(wiphy, "set regdomain successful\n");

	memcpy(rwnx_hw->custregd.working_country, country_code, 2);

	ret = rwnx_send_country_code_cfg(rwnx_hw);
	if (ret) {
		wiphy_err(wiphy, "set power table to mcu faied %d.\n", ret);
	}

	if (rwnx_wlan_enabled(rwnx_hw))
		rwnx_send_me_chan_config_req(rwnx_hw);

	return memcmp(ori_country_code, country_code, 2);
}

