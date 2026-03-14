// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

	Blipverbpulsar,	// pointer to sample array
	Blipverbpulsar_SIZE,	// size of the sample array
	Blipverbpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Blipverbpulsar",	// sample name

	Bass_insect,	// pointer to sample array
	Bass_insect_SIZE,	// size of the sample array
	Bass_insect_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bass_insect",	// sample name

	Clapdarkpulsar,	// pointer to sample array
	Clapdarkpulsar_SIZE,	// size of the sample array
	Clapdarkpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clapdarkpulsar",	// sample name

	Chzipdivepulsar,	// pointer to sample array
	Chzipdivepulsar_SIZE,	// size of the sample array
	Chzipdivepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chzipdivepulsar",	// sample name

	Wavepluck_a,	// pointer to sample array
	Wavepluck_a_SIZE,	// size of the sample array
	Wavepluck_a_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Wavepluck_a",	// sample name

	Sddelaycrackpulsar,	// pointer to sample array
	Sddelaycrackpulsar_SIZE,	// size of the sample array
	Sddelaycrackpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Sddelaycrackpulsar",	// sample name

	Clapstandardpulsar,	// pointer to sample array
	Clapstandardpulsar_SIZE,	// size of the sample array
	Clapstandardpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clapstandardpulsar",	// sample name

	Chclassicpulsar05,	// pointer to sample array
	Chclassicpulsar05_SIZE,	// size of the sample array
	Chclassicpulsar05_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chclassicpulsar05",	// sample name

	Sdclaphardpulsar,	// pointer to sample array
	Sdclaphardpulsar_SIZE,	// size of the sample array
	Sdclaphardpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Sdclaphardpulsar",	// sample name

	Rimverbdarkpulsar,	// pointer to sample array
	Rimverbdarkpulsar_SIZE,	// size of the sample array
	Rimverbdarkpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Rimverbdarkpulsar",	// sample name

};
