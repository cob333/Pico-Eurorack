#pragma once
#include "TR-808/_snare_tr808.h"
#include "TR-808/_cowbell_tr808.h"
#include "TR-808/_tomlow_tr808.h"
#include "TR-808/_cymbal_tr808.h"
#include "TR-808/_congamid_tr808.h"
#include "TR-808/_clap_tr808.h"
#include "TR-808/_hihatopen_tr808.h"
#include "TR-808/_rim_tr808.h"
#include "TR-808/_maracas_tr808.h"
#include "TR-808/_tomhigh_tr808.h"
#include "TR-808/_kick_tr808.h"
#include "TR-808/_hihat_tr808.h"
#include "TR-808/_clave_tr808.h"
#include "TR-808/_tommid_tr808.h"

static sample_t bank_tr_808_samples[] = {

	_snare_tr808,	// pointer to sample array
	_snare_tr808_SIZE,	// size of the sample array
	_snare_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_snare_tr808",	// sample name

	_cowbell_tr808,	// pointer to sample array
	_cowbell_tr808_SIZE,	// size of the sample array
	_cowbell_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_cowbell_tr808",	// sample name

	_tomlow_tr808,	// pointer to sample array
	_tomlow_tr808_SIZE,	// size of the sample array
	_tomlow_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomlow_tr808",	// sample name

	_cymbal_tr808,	// pointer to sample array
	_cymbal_tr808_SIZE,	// size of the sample array
	_cymbal_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_cymbal_tr808",	// sample name

	_congamid_tr808,	// pointer to sample array
	_congamid_tr808_SIZE,	// size of the sample array
	_congamid_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_congamid_tr808",	// sample name

	_clap_tr808,	// pointer to sample array
	_clap_tr808_SIZE,	// size of the sample array
	_clap_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_clap_tr808",	// sample name

	_hihatopen_tr808,	// pointer to sample array
	_hihatopen_tr808_SIZE,	// size of the sample array
	_hihatopen_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihatopen_tr808",	// sample name

	_rim_tr808,	// pointer to sample array
	_rim_tr808_SIZE,	// size of the sample array
	_rim_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_rim_tr808",	// sample name

	_maracas_tr808,	// pointer to sample array
	_maracas_tr808_SIZE,	// size of the sample array
	_maracas_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_maracas_tr808",	// sample name

	_tomhigh_tr808,	// pointer to sample array
	_tomhigh_tr808_SIZE,	// size of the sample array
	_tomhigh_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tomhigh_tr808",	// sample name

	_kick_tr808,	// pointer to sample array
	_kick_tr808_SIZE,	// size of the sample array
	_kick_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	45,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_kick_tr808",	// sample name

	_hihat_tr808,	// pointer to sample array
	_hihat_tr808_SIZE,	// size of the sample array
	_hihat_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	46,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_hihat_tr808",	// sample name

	_clave_tr808,	// pointer to sample array
	_clave_tr808_SIZE,	// size of the sample array
	_clave_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	47,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_clave_tr808",	// sample name

	_tommid_tr808,	// pointer to sample array
	_tommid_tr808_SIZE,	// size of the sample array
	_tommid_tr808_SIZE,	//sampleindex. if at end of sample array sound is not playing
	48,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"_tommid_tr808",	// sample name

};

static const uint16_t bank_tr_808_count = (uint16_t)(sizeof(bank_tr_808_samples) / sizeof(sample_t));
static const char * const bank_tr_808_name = "TR-808";
