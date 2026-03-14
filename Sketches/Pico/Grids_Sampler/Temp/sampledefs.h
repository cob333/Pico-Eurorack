// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

	Percxax,	// pointer to sample array
	Percxax_SIZE,	// size of the sample array
	Percxax_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Percxax",	// sample name

	Percrodeo,	// pointer to sample array
	Percrodeo_SIZE,	// size of the sample array
	Percrodeo_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Percrodeo",	// sample name

	Openhatpbsadults,	// pointer to sample array
	Openhatpbsadults_SIZE,	// size of the sample array
	Openhatpbsadults_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Openhatpbsadults",	// sample name

	Hihatsnow,	// pointer to sample array
	Hihatsnow_SIZE,	// size of the sample array
	Hihatsnow_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Hihatsnow",	// sample name

	Percflick,	// pointer to sample array
	Percflick_SIZE,	// size of the sample array
	Percflick_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Percflick",	// sample name

	Perc16cowbell,	// pointer to sample array
	Perc16cowbell_SIZE,	// size of the sample array
	Perc16cowbell_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Perc16cowbell",	// sample name

	Perchobby,	// pointer to sample array
	Perchobby_SIZE,	// size of the sample array
	Perchobby_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Perchobby",	// sample name

	Clap6ixty,	// pointer to sample array
	Clap6ixty_SIZE,	// size of the sample array
	Clap6ixty_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clap6ixty",	// sample name

	Synbendpulsar,	// pointer to sample array
	Synbendpulsar_SIZE,	// size of the sample array
	Synbendpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Synbendpulsar",	// sample name

	Timesstab,	// pointer to sample array
	Timesstab_SIZE,	// size of the sample array
	Timesstab_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Timesstab",	// sample name

	Chainbass2,	// pointer to sample array
	Chainbass2_SIZE,	// size of the sample array
	Chainbass2_SIZE,	//sampleindex. if at end of sample array sound is not playing
	45,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chainbass2",	// sample name

	Lofisweeppulsar,	// pointer to sample array
	Lofisweeppulsar_SIZE,	// size of the sample array
	Lofisweeppulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	46,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Lofisweeppulsar",	// sample name

};
