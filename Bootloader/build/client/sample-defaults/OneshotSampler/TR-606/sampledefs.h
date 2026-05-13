// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

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
