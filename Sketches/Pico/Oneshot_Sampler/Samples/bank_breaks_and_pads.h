#pragma once
#include "Breaks_And_Pads/Godchordpad.h"
#include "Breaks_And_Pads/Pressinbreak.h"
#include "Breaks_And_Pads/Tenbreak2.h"
#include "Breaks_And_Pads/Nighttrainshortpad.h"
#include "Breaks_And_Pads/Amenbreak120bpm.h"

static sample_t bank_breaks_and_pads_samples[] = {

	Godchordpad,	// pointer to sample array
	Godchordpad_SIZE,	// size of the sample array
	Godchordpad_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Godchordpad",	// sample name

	Pressinbreak,	// pointer to sample array
	Pressinbreak_SIZE,	// size of the sample array
	Pressinbreak_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Pressinbreak",	// sample name

	Tenbreak2,	// pointer to sample array
	Tenbreak2_SIZE,	// size of the sample array
	Tenbreak2_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Tenbreak2",	// sample name

	Nighttrainshortpad,	// pointer to sample array
	Nighttrainshortpad_SIZE,	// size of the sample array
	Nighttrainshortpad_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Nighttrainshortpad",	// sample name

	Amenbreak120bpm,	// pointer to sample array
	Amenbreak120bpm_SIZE,	// size of the sample array
	Amenbreak120bpm_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Amenbreak120bpm",	// sample name

};

static const uint16_t bank_breaks_and_pads_count = (uint16_t)(sizeof(bank_breaks_and_pads_samples) / sizeof(sample_t));
static const char * const bank_breaks_and_pads_name = "Breaks_And_Pads";
