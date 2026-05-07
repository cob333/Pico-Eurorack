#pragma once
#include "TR-909/_hihatopen_tr909.h"
#include "TR-909/_clap_tr909.h"
#include "TR-909/_rim_tr909.h"
#include "TR-909/_tomlow_tr909.h"
#include "TR-909/_snare_tr909.h"
#include "TR-909/_tommid_tr909.h"
#include "TR-909/_hihat_tr909.h"
#include "TR-909/_tomshort_tr909.h"
#include "TR-909/_kick_tr909.h"
#include "TR-909/_crash_tr909.h"
#include "TR-909/_tomhigh_tr909.h"
#include "TR-909/_ride_tr909.h"

static sample_t bank_tr_909_samples[] = {

	_hihatopen_tr909,	// pointer to sample array
	_hihatopen_tr909_SIZE,	// size of the sample array
	_hihatopen_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihatopen_tr909",	// sample name

	_clap_tr909,	// pointer to sample array
	_clap_tr909_SIZE,	// size of the sample array
	_clap_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_clap_tr909",	// sample name

	_rim_tr909,	// pointer to sample array
	_rim_tr909_SIZE,	// size of the sample array
	_rim_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_rim_tr909",	// sample name

	_tomlow_tr909,	// pointer to sample array
	_tomlow_tr909_SIZE,	// size of the sample array
	_tomlow_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomlow_tr909",	// sample name

	_snare_tr909,	// pointer to sample array
	_snare_tr909_SIZE,	// size of the sample array
	_snare_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_snare_tr909",	// sample name

	_tommid_tr909,	// pointer to sample array
	_tommid_tr909_SIZE,	// size of the sample array
	_tommid_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tommid_tr909",	// sample name

	_hihat_tr909,	// pointer to sample array
	_hihat_tr909_SIZE,	// size of the sample array
	_hihat_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihat_tr909",	// sample name

	_tomshort_tr909,	// pointer to sample array
	_tomshort_tr909_SIZE,	// size of the sample array
	_tomshort_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomshort_tr909",	// sample name

	_kick_tr909,	// pointer to sample array
	_kick_tr909_SIZE,	// size of the sample array
	_kick_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_kick_tr909",	// sample name

	_crash_tr909,	// pointer to sample array
	_crash_tr909_SIZE,	// size of the sample array
	_crash_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_crash_tr909",	// sample name

	_tomhigh_tr909,	// pointer to sample array
	_tomhigh_tr909_SIZE,	// size of the sample array
	_tomhigh_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	45,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomhigh_tr909",	// sample name

	_ride_tr909,	// pointer to sample array
	_ride_tr909_SIZE,	// size of the sample array
	_ride_tr909_SIZE,	//sampleindex. if at end of sample array sound is not playing
	46,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_ride_tr909",	// sample name

};

static const uint16_t bank_tr_909_count = (uint16_t)(sizeof(bank_tr_909_samples) / sizeof(sample_t));
static const char * const bank_tr_909_name = "TR-909";
