// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/packetstats.h>
#include <chiaki/log.h>
#include <assert.h>

CHIAKI_EXPORT ChiakiErrorCode chiaki_packet_stats_init(ChiakiPacketStats *stats)
{
	ChiakiErrorCode err = chiaki_mutex_init(&stats->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_mutex_lock(&stats->mutex);
	assert(err == CHIAKI_ERR_SUCCESS);
	stats->gen_received = 0;
	stats->gen_lost = 0;
	stats->seq_min = 0;
	stats->seq_max = 0;
	stats->seq_received = 0;
	err = chiaki_mutex_unlock(&stats->mutex);
	return err;
}

CHIAKI_EXPORT void chiaki_packet_stats_fini(ChiakiPacketStats *stats)
{
	chiaki_mutex_fini(&stats->mutex);
}

static void reset_stats(ChiakiPacketStats *stats)
{
	stats->gen_received = 0;
	stats->gen_lost = 0;
	stats->seq_min = stats->seq_max;
	stats->seq_received = 0;
}

CHIAKI_EXPORT void chiaki_packet_stats_reset(ChiakiPacketStats *stats)
{
	chiaki_mutex_lock(&stats->mutex);
	reset_stats(stats);
	chiaki_mutex_unlock(&stats->mutex);
}

CHIAKI_EXPORT void chiaki_packet_stats_push_generation(ChiakiPacketStats *stats, uint64_t received, uint64_t lost)
{
	chiaki_mutex_lock(&stats->mutex);
	stats->gen_received += received;
	stats->gen_lost += lost;
	chiaki_mutex_unlock(&stats->mutex);
}

CHIAKI_EXPORT void chiaki_packet_stats_push_seq(ChiakiPacketStats *stats, ChiakiSeqNum16 seq_num)
{
	stats->seq_received++;
	if(chiaki_seq_num_16_gt(seq_num, stats->seq_max))
		stats->seq_max = seq_num;
}

CHIAKI_EXPORT void chiaki_packet_stats_get(ChiakiPacketStats *stats, bool reset, uint64_t *received, uint64_t *lost)
{
	chiaki_mutex_lock(&stats->mutex);

	// gen
	*received = stats->gen_received;
	*lost = stats->gen_lost;

	//CHIAKI_LOGD(NULL, "gen received: %llu, lost: %llu",
	//		(unsigned long long)stats->gen_received,
	//		(unsigned long long)stats->gen_lost);

	// seq
	// The sequence numbers are 16 bits: take the difference in 16 bits so a wrap
	// (the audio frame index wraps about every 11 minutes) is not counted as
	// ~2^64 lost packets, which made the congestion report claim maximum loss.
	uint64_t seq_diff = (ChiakiSeqNum16)(stats->seq_max - stats->seq_min);
	uint64_t seq_lost = stats->seq_received >= seq_diff ? 0 : seq_diff - stats->seq_received;
	*received += stats->seq_received;
	*lost += seq_lost;

	//CHIAKI_LOGD(NULL, "seq received: %llu, lost: %llu",
	//		(unsigned long long)stats->seq_received,
	//		(unsigned long long)seq_lost);

	if(reset)
		reset_stats(stats);
	chiaki_mutex_unlock(&stats->mutex);
}
