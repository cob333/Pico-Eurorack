#pragma once
#include "TR-606/_snare_tr606.h"
#include "TR-606/_tomlow_tr606.h"
#include "TR-606/_clap_tr606.h"
#include "TR-606/_hihatopen_tr606.h"
#include "TR-606/_ride_tr606.h"
#include "TR-606/_rim_tr606.h"
#include "TR-606/_hihatpedal_tr606.h"
#include "TR-606/_tomhigh_tr606.h"
#include "TR-606/_crash_tr606.h"
#include "TR-606/_kick_tr606.h"
#include "TR-606/_hihat_tr606.h"

static sample_t bank_tr_606_samples[] = {

	_snare_tr606,	// pointer to sample array
	_snare_tr606_SIZE,	// size of the sample array
	_snare_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_snare_tr606",	// sample name

	_tomlow_tr606,	// pointer to sample array
	_tomlow_tr606_SIZE,	// size of the sample array
	_tomlow_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomlow_tr606",	// sample name

	_clap_tr606,	// pointer to sample array
	_clap_tr606_SIZE,	// size of the sample array
	_clap_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_clap_tr606",	// sample name

	_hihatopen_tr606,	// pointer to sample array
	_hihatopen_tr606_SIZE,	// size of the sample array
	_hihatopen_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihatopen_tr606",	// sample name

	_ride_tr606,	// pointer to sample array
	_ride_tr606_SIZE,	// size of the sample array
	_ride_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_ride_tr606",	// sample name

	_rim_tr606,	// pointer to sample array
	_rim_tr606_SIZE,	// size of the sample array
	_rim_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_rim_tr606",	// sample name

	_hihatpedal_tr606,	// pointer to sample array
	_hihatpedal_tr606_SIZE,	// size of the sample array
	_hihatpedal_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihatpedal_tr606",	// sample name

	_tomhigh_tr606,	// pointer to sample array
	_tomhigh_tr606_SIZE,	// size of the sample array
	_tomhigh_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomhigh_tr606",	// sample name

	_crash_tr606,	// pointer to sample array
	_crash_tr606_SIZE,	// size of the sample array
	_crash_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_crash_tr606",	// sample name

	_kick_tr606,	// pointer to sample array
	_kick_tr606_SIZE,	// size of the sample array
	_kick_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_kick_tr606",	// sample name

	_hihat_tr606,	// pointer to sample array
	_hihat_tr606_SIZE,	// size of the sample array
	_hihat_tr606_SIZE,	//sampleindex. if at end of sample array sound is not playing
	45,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihat_tr606",	// sample name

};

static const uint16_t bank_tr_606_count = (uint16_t)(sizeof(bank_tr_606_samples) / sizeof(sample_t));
static const char * const bank_tr_606_name = "TR-606";
