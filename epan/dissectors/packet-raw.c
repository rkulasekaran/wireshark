/* packet-raw.c
 * Routines for raw packet disassembly
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 *
 * This file created and by Mike Hall <mlh@io.com>
 * Copyright 1998
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib.h>
#include <epan/packet.h>
#include <epan/expert.h>
#include <wiretap/wtap.h>
#include "packet-raw.h"
#include "packet-ip.h"
#include "packet-ppp.h"

void proto_register_raw(void);
void proto_reg_handoff_raw(void);

static int proto_raw = -1;
static gint ett_raw = -1;

static expert_field ei_raw_no_link = EI_INIT;

static const char zeroes[10] = {0,0,0,0,0,0,0,0,0,0};

static dissector_handle_t ip_handle;
static dissector_handle_t ipv6_handle;
static dissector_handle_t data_handle;
static dissector_handle_t ppp_hdlc_handle;

void
capture_raw(const guchar *pd, int len, packet_counts *ld)
{
  /* So far, the only time we get raw connection types are with Linux and
   * Irix PPP connections.  We can't tell what type of data is coming down
   * the line, so our safest bet is IP. - GCC
   */

  /* Currently, the Linux 2.1.xxx PPP driver passes back some of the header
   * sometimes.  This check should be removed when 2.2 is out.
   */
  if (BYTES_ARE_IN_FRAME(0,len,2) && pd[0] == 0xff && pd[1] == 0x03) {
    capture_ppp_hdlc(pd, 0, len, ld);
  }
  /* The Linux ISDN driver sends a fake MAC address before the PPP header
   * on its ippp interfaces... */
  else if (BYTES_ARE_IN_FRAME(0,len,8) && pd[6] == 0xff && pd[7] == 0x03) {
    capture_ppp_hdlc(pd, 6, len, ld);
  }
  /* ...except when it just puts out one byte before the PPP header... */
  else if (BYTES_ARE_IN_FRAME(0,len,3) && pd[1] == 0xff && pd[2] == 0x03) {
    capture_ppp_hdlc(pd, 1, len, ld);
  }
  /* ...and if the connection is currently down, it sends 10 bytes of zeroes
   * instead of a fake MAC address and PPP header. */
  else if (BYTES_ARE_IN_FRAME(0,len,10) && memcmp(pd, zeroes, 10) == 0) {
    capture_ip(pd, 10, len, ld);
  }
  else {
    /*
     * OK, is this IPv4 or IPv6?
     */
    if (BYTES_ARE_IN_FRAME(0,len,1)) {
      switch (pd[0] & 0xF0) {

      case 0x40:
        /* IPv4 */
        capture_ip(pd, 0, len, ld);
        break;

#if 0
      case 0x60:
        /* IPv6 */
        capture_ipv6(pd, 0, len, ld);
        break;
#endif
      }
    }
  }
}

static void
dissect_raw(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  proto_item    *ti;
  tvbuff_t      *next_tvb;

  /* load the top pane info. This should be overwritten by
     the next protocol in the stack */
  col_set_str(pinfo->cinfo, COL_RES_DL_SRC, "N/A");
  col_set_str(pinfo->cinfo, COL_RES_DL_DST, "N/A");
  col_set_str(pinfo->cinfo, COL_PROTOCOL, "N/A");
  col_set_str(pinfo->cinfo, COL_INFO, "Raw packet data");

  /* populate a tree in the second pane with the status of the link
     layer (ie none) */
  ti = proto_tree_add_item(tree, proto_raw, tvb, 0, 0, ENC_NA);
  expert_add_info(pinfo, ti, &ei_raw_no_link);

  if (pinfo->fd->lnk_t == WTAP_ENCAP_RAW_IP4) {
    call_dissector(ip_handle, tvb, pinfo, tree);
  }
  else if (pinfo->fd->lnk_t == WTAP_ENCAP_RAW_IP6) {
    call_dissector(ipv6_handle, tvb, pinfo, tree);
  }
  else

  /* So far, the only time we get raw connection types are with Linux and
   * Irix PPP connections.  We can't tell what type of data is coming down
   * the line, so our safest bet is IP. - GCC
   */

  /* Currently, the Linux 2.1.xxx PPP driver passes back some of the header
   * sometimes.  This check should be removed when 2.2 is out.
   */
  if (tvb_get_ntohs(tvb, 0) == 0xff03) {
    call_dissector(ppp_hdlc_handle, tvb, pinfo, tree);
  }
  /* The Linux ISDN driver sends a fake MAC address before the PPP header
   * on its ippp interfaces... */
  else if (tvb_get_ntohs(tvb, 6) == 0xff03) {
    next_tvb = tvb_new_subset_remaining(tvb, 6);
    call_dissector(ppp_hdlc_handle, next_tvb, pinfo, tree);
  }
  /* ...except when it just puts out one byte before the PPP header... */
  else if (tvb_get_ntohs(tvb, 1) == 0xff03) {
    next_tvb = tvb_new_subset_remaining(tvb, 1);
    call_dissector(ppp_hdlc_handle, next_tvb, pinfo, tree);
  }
  /* ...and if the connection is currently down, it sends 10 bytes of zeroes
   * instead of a fake MAC address and PPP header. */
  else if (tvb_memeql(tvb, 0, zeroes,10) == 0) {
    next_tvb = tvb_new_subset_remaining(tvb, 10);
    call_dissector(ip_handle, next_tvb, pinfo, tree);
  }
  else {
    /*
     * OK, is this IPv4 or IPv6?
     */
    switch (tvb_get_guint8(tvb, 0) & 0xF0) {

    case 0x40:
      /* IPv4 */
      call_dissector(ip_handle, tvb, pinfo, tree);
      break;

    case 0x60:
      /* IPv6 */
      call_dissector(ipv6_handle, tvb, pinfo, tree);
      break;

    default:
      /* None of the above. */
      call_dissector(data_handle, tvb, pinfo, tree);
      break;
    }
  }
}

void
proto_register_raw(void)
{
  static gint *ett[] = {
    &ett_raw,
  };

  static ei_register_info ei[] = {
    { &ei_raw_no_link, { "raw.no_link", PI_PROTOCOL, PI_NOTE, "No link information available", EXPFILL }},
  };

  expert_module_t* expert_raw;

  proto_raw = proto_register_protocol("Raw packet data", "Raw", "raw");
  proto_register_subtree_array(ett, array_length(ett));
  expert_raw = expert_register_protocol(proto_raw);
  expert_register_field_array(expert_raw, ei, array_length(ei));
}

void
proto_reg_handoff_raw(void)
{
  dissector_handle_t raw_handle;

  /*
   * Get handles for the IP, IPv6, undissected-data, and
   * PPP-in-HDLC-like-framing dissectors.
   */
  ip_handle = find_dissector("ip");
  ipv6_handle = find_dissector("ipv6");
  data_handle = find_dissector("data");
  ppp_hdlc_handle = find_dissector("ppp_hdlc");
  raw_handle = create_dissector_handle(dissect_raw, proto_raw);
  dissector_add_uint("wtap_encap", WTAP_ENCAP_RAW_IP, raw_handle);
  dissector_add_uint("wtap_encap", WTAP_ENCAP_RAW_IP4, raw_handle);
  dissector_add_uint("wtap_encap", WTAP_ENCAP_RAW_IP6, raw_handle);
}

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
