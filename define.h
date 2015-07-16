#ifndef __define__
#define __define__

#define LOG_INFO(...) {printf(__VA_ARGS__);printf("\n");}
#define LOG_ERR(...) {printf(__VA_ARGS__);}
//windows
#define snprintf _snprintf

#include "libtorrent/extensions/metadata_transfer.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/extensions/smart_ban.hpp"

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/time.hpp"

#include "libtorrent/create_torrent.hpp"

#include "boost/filesystem.hpp"

#endif