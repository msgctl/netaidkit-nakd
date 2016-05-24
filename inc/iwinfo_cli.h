/*
 * This implementation has been copied from iwinfo library and adapted for
 * the NetAidKit project, licensed under compatible GNU General Public License
 * version 2.
 *
 * Original file: iwinfo_cli.c @ e4aca39
 *
 * iwinfo library is available at: git://git.openwrt.org/project/iwinfo.git
 */

/*
 * iwinfo - Wireless Information Library - Command line frontend
 *
 *   Copyright (C) 2011 Jo-Philipp Wich <xm@subsignal.org>
 *
 * The iwinfo library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * The iwinfo library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the iwinfo library. If not, see http://www.gnu.org/licenses/.
 */

#include <iwinfo.h>

static char * format_bssid(unsigned char *mac)
{
	static char buf[18];

	snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return buf;
}

static char * format_ssid(char *ssid)
{
	static char buf[IWINFO_ESSID_MAX_SIZE+3];

	if (ssid && ssid[0])
		snprintf(buf, sizeof(buf), "\"%s\"", ssid);
	else
		snprintf(buf, sizeof(buf), "unknown");

	return buf;
}

static char * format_channel(int ch)
{
	static char buf[8];

	if (ch <= 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d", ch);

	return buf;
}

static char * format_frequency(int freq)
{
	static char buf[10];

	if (freq <= 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%.3f GHz", ((float)freq / 1000.0));

	return buf;
}

static char * format_txpower(int pwr)
{
	static char buf[10];

	if (pwr < 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d dBm", pwr);

	return buf;
}

static char * format_quality(int qual)
{
	static char buf[8];

	if (qual < 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d", qual);

	return buf;
}

static char * format_quality_max(int qmax)
{
	static char buf[8];

	if (qmax < 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d", qmax);

	return buf;
}

static char * format_signal(int sig)
{
	static char buf[10];

	if (!sig)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d dBm", sig);

	return buf;
}

static char * format_noise(int noise)
{
	static char buf[10];

	if (!noise)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d dBm", noise);

	return buf;
}

static char * format_rate(int rate)
{
	static char buf[14];

	if (rate <= 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "%d.%d MBit/s",
			rate / 1000, (rate % 1000) / 100);

	return buf;
}

static char * format_enc_ciphers(int ciphers)
{
	static char str[128] = { 0 };
	char *pos = str;

	if (ciphers & IWINFO_CIPHER_WEP40)
		pos += sprintf(pos, "WEP-40, ");

	if (ciphers & IWINFO_CIPHER_WEP104)
		pos += sprintf(pos, "WEP-104, ");

	if (ciphers & IWINFO_CIPHER_TKIP)
		pos += sprintf(pos, "TKIP, ");

	if (ciphers & IWINFO_CIPHER_CCMP)
		pos += sprintf(pos, "CCMP, ");

	if (ciphers & IWINFO_CIPHER_WRAP)
		pos += sprintf(pos, "WRAP, ");

	if (ciphers & IWINFO_CIPHER_AESOCB)
		pos += sprintf(pos, "AES-OCB, ");

	if (ciphers & IWINFO_CIPHER_CKIP)
		pos += sprintf(pos, "CKIP, ");

	if (!ciphers || (ciphers & IWINFO_CIPHER_NONE))
		pos += sprintf(pos, "NONE, ");

	*(pos - 2) = 0;

	return str;
}

static char * format_enc_suites(int suites)
{
	static char str[64] = { 0 };
	char *pos = str;

	if (suites & IWINFO_KMGMT_PSK)
		pos += sprintf(pos, "PSK/");

	if (suites & IWINFO_KMGMT_8021x)
		pos += sprintf(pos, "802.1X/");

	if (!suites || (suites & IWINFO_KMGMT_NONE))
		pos += sprintf(pos, "NONE/");

	*(pos - 1) = 0;

	return str;
}

static char * format_encryption(struct iwinfo_crypto_entry *c)
{
	static char buf[512];

	if (!c)
	{
		snprintf(buf, sizeof(buf), "unknown");
	}
	else if (c->enabled)
	{
		/* WEP */
		if (c->auth_algs && !c->wpa_version)
		{
			if ((c->auth_algs & IWINFO_AUTH_OPEN) &&
				(c->auth_algs & IWINFO_AUTH_SHARED))
			{
				snprintf(buf, sizeof(buf), "WEP Open/Shared (%s)",
					format_enc_ciphers(c->pair_ciphers));
			}
			else if (c->auth_algs & IWINFO_AUTH_OPEN)
			{
				snprintf(buf, sizeof(buf), "WEP Open System (%s)",
					format_enc_ciphers(c->pair_ciphers));
			}
			else if (c->auth_algs & IWINFO_AUTH_SHARED)
			{
				snprintf(buf, sizeof(buf), "WEP Shared Auth (%s)",
					format_enc_ciphers(c->pair_ciphers));
			}
		}

		/* WPA */
		else if (c->wpa_version)
		{
			switch (c->wpa_version) {
				case 3:
					snprintf(buf, sizeof(buf), "mixed WPA/WPA2 %s (%s)",
						format_enc_suites(c->auth_suites),
						format_enc_ciphers(c->pair_ciphers | c->group_ciphers));
					break;

				case 2:
					snprintf(buf, sizeof(buf), "WPA2 %s (%s)",
						format_enc_suites(c->auth_suites),
						format_enc_ciphers(c->pair_ciphers | c->group_ciphers));
					break;

				case 1:
					snprintf(buf, sizeof(buf), "WPA %s (%s)",
						format_enc_suites(c->auth_suites),
						format_enc_ciphers(c->pair_ciphers | c->group_ciphers));
					break;
			}
		}
		else
		{
			snprintf(buf, sizeof(buf), "none");
		}
	}
	else
	{
		snprintf(buf, sizeof(buf), "none");
	}

	return buf;
}

static char * format_hwmodes(int modes)
{
	static char buf[12];

	if (modes <= 0)
		snprintf(buf, sizeof(buf), "unknown");
	else
		snprintf(buf, sizeof(buf), "802.11%s%s%s%s%s",
			(modes & IWINFO_80211_A) ? "a" : "",
			(modes & IWINFO_80211_B) ? "b" : "",
			(modes & IWINFO_80211_G) ? "g" : "",
			(modes & IWINFO_80211_N) ? "n" : "",
			(modes & IWINFO_80211_AC) ? "ac" : "");

	return buf;
}
