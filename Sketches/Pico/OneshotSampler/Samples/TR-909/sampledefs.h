// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

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
